/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* appunti.c -- il lato host degli appunti condivisi: il server, la sorveglianza
 * degli appunti di Windows, e il cablaggio fra i due.
 *
 * COSA NON STA QUI, ed e' la ragione per cui questo file resta leggibile: il
 * formato della trama, il tetto e la guardia contro l'eco stanno in
 * appunti-trama.c, dove si provano senza rete e senza VM. Qui c'e' solo cio' che
 * una prova non puo' toccare -- Winsock, WM_CLIPBOARDUPDATE, due thread -- e
 * quando qualcosa di deciso finisce in questo file, quella decisione smette di
 * essere provata.
 *
 * I DUE THREAD, e chi tocca cosa. Il thread del SERVER (creato qui) possiede i
 * socket, il buffer di lettura e la composizione delle trame; il thread della
 * FINESTRA (quello di main.c) possiede gli appunti di Windows. Nessuno dei due
 * fa il lavoro dell'altro, e i due punti in cui si passano qualcosa sono i soli
 * protetti dal lucchetto:
 *
 *   guest  -> Windows: ap_verso_windows, consegnato con PostMessage perche'
 *                      SetClipboardData VUOLE il thread che possiede la
 *                      finestra -- da un altro thread non fallisce con un
 *                      errore, fa una cosa diversa;
 *   Windows -> guest:  ap_verso_guest, che il thread del server raccoglie al
 *                      giro successivo. Il thread della finestra NON chiama mai
 *                      send(): un guest che smette di leggere terrebbe ferma la
 *                      send fino alla propria scadenza, e con essa il pannello
 *                      del registro. E' la stessa ragione per cui il server non
 *                      sta sul thread della finestra.
 *
 * LA MEMORIA. Il tetto e' 1 MiB e i buffer che lo devono contenere sono sei
 * (lettura, estrazione, composizione, i due di consegna, e le due guardie
 * contro l'eco che ricordano un valore intero per verso): ~7 MiB di BSS, cioe'
 * pagine azzerate che il sistema materializza solo quando si copia davvero, in
 * un processo che poco dopo avvia una VM da 6 GB. Allocarle a runtime avrebbe
 * aggiunto un modo di fallire per risparmiare pagine che non si toccano.
 */
#include <winsock2.h>   /* PRIMA di windows.h: incluso dopo, windows.h tira
                         * dentro il winsock.h di prima generazione e i due
                         * header ridefiniscono le stesse struct. L'errore che
                         * ne esce nomina fd_set, non l'ordine delle
                         * inclusioni. guscio.h include windows.h a sua volta,
                         * quindi questa riga deve stare anche prima di quello. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"
#include "appunti-trama.h"

/* Ogni quanto il thread del server si sveglia quando non succede niente.
 *
 * PERCHE' UN RISVEGLIO E NON UN RISVEGLIO SU RICHIESTA: il thread deve fare tre
 * cose (accettare, leggere, mandare cio' che la finestra ha lasciato) e solo le
 * prime due hanno un descrittore su cui select possa aspettare. Svegliare la
 * select dalla terza vorrebbe dire una coppia di socket collegati a se stessi
 * solo per suonare il campanello. Il costo di questa scelta e' un risveglio
 * ogni 200 ms su un thread che dorme, e fino a 200 ms di ritardo fra il Ctrl+C
 * su Windows e l'arrivo nel guest: invisibile a chi poi deve incollare. */
#define AP_PASSO_MS 200

/* Quanto si concede a una send prima di dichiararla persa.
 *
 * SERVE PERCHE' ALTRIMENTI IL SERVER SI PIANTA: un guest che smette di leggere
 * riempie la finestra di ricezione, la send si ferma per sempre, e da li' in poi
 * il thread non accetta piu' nessuna connessione nuova -- cioe' proprio la via
 * con cui un guest riavviato si ripara da se'. Scaduta la send, la connessione
 * si CHIUDE e non si riprova: dopo una send scaduta non si sa quanti byte siano
 * partiti, quindi il flusso ha perso l'allineamento fra le trame e l'unica cosa
 * onesta e' ricominciare. */
#define AP_INVIO_MS 2000

/* La porta che l'APK del guest ha COMPILATA DENTRO (PORTA in
 * guest/appunti-guest/src/com/godziller/habumi/clipboard/ServizioAppunti.java). Serve solo
 * a poter avvisare quando porta_appunti se ne discosta: vedi appunti_avvia.
 * Se un giorno si cambia la costante di la', va cambiata anche qui -- e finche'
 * sono due numeri in due linguaggi, l'unica difesa e' che il guscio se ne
 * accorga e lo scriva. */
#define AP_PORTA_APK 15556

/* --- lo stato ------------------------------------------------------------ */

static SOCKET ap_ascolto = INVALID_SOCKET;
/* UNA SOLA connessione per volta. Il thread del server e' l'unico che la scrive;
 * il thread della finestra la legge (per sapere se c'e' qualcuno) sotto il
 * lucchetto, perche' fra il suo controllo e la chiusura da parte dell'altro
 * thread ci sta un istante. */
static SOCKET ap_cliente = INVALID_SOCKET;
static HANDLE ap_thread_h;
static volatile LONG ap_fermare;
static CRITICAL_SECTION ap_lucchetto;
static bool ap_acceso;

/* Il buffer di lettura, e i byte gia' arrivati che non formano ancora una trama
 * intera. Grande quanto la trama piu' grande che il tetto permette: cosi' una
 * trama al tetto ci sta sempre per intero, e "non ci sta" non e' uno stato che
 * questo codice debba saper gestire. Solo il thread del server lo tocca. */
static unsigned char ap_rx[AP_TRAMA_INTESTAZIONE + AP_TETTO_BYTE];
static size_t ap_rx_n;
/* Dove ap_trama_estrai consegna il testo, e dove si compone quello in uscita.
 * Entrambi del solo thread del server. */
static char ap_rx_testo[AP_TETTO_BYTE];
static unsigned char ap_tx[AP_TRAMA_INTESTAZIONE + AP_TETTO_BYTE];

/* Consegna server -> finestra. */
static char ap_verso_windows[AP_TETTO_BYTE];
static size_t ap_verso_windows_n;
static bool ap_verso_windows_pronto;

/* Consegna finestra -> server. Un byte in piu' del tetto perche' qui ci finisce
 * la conversione da UTF-16, che porta con se' il terminatore: la TRAMA non lo
 * trasporta, ma WideCharToMultiByte lo scrive comunque e senza questo byte un
 * testo esattamente al tetto uscirebbe dal buffer di uno. */
static char ap_verso_guest[AP_TETTO_BYTE + 1];
static size_t ap_verso_guest_n;
static bool ap_verso_guest_pronto;

/* LE DUE GUARDIE CONTRO L'ECO, una per verso, e servono entrambe.
 *
 * ap_eco_dal_guest ricorda l'ultimo testo ARRIVATO dal guest e viene consultato
 * prima di mandare: senza, scrivere negli appunti di Windows fa scattare
 * WM_CLIPBOARDUPDATE, quel testo torna al guest, il guest lo riscrive nei suoi
 * appunti, il suo listener scatta, e i due lati si rimbalzano la stessa riga per
 * sempre. E' il modo in cui questa funzione si rompe.
 *
 * ap_eco_verso_guest ricorda l'ultimo testo MANDATO e viene consultato prima di
 * scrivere negli appunti di Windows: chiude lo stesso anello percorso
 * nell'altro senso, e non si appoggia al fatto che l'app del guest abbia la
 * propria guardia. Una guardia sola avrebbe funzionato finche' l'altro lato
 * fosse stato scritto bene, cioe' avrebbe reso il difetto piu' raro invece di
 * impossibile.
 *
 * Statiche e non locali: ApEco pesa un AP_TETTO_BYTE pieno e lo stack di default
 * su Windows e' 1 MiB -- una di queste su una pila salta prima della prima riga.
 *
 * ap_eco_verso_guest la tocca solo il thread del server (la scrive quando manda,
 * la legge quando riceve). ap_eco_dal_guest la scrive il server e la legge la
 * finestra, quindi passa dal lucchetto. */
static ApEco ap_eco_dal_guest;
static ApEco ap_eco_verso_guest;

/* "L'assenza del guest e' gia' stata detta". Vedi appunti_host_cambiati per
 * perche' quella riga non si ripete. Sotto lucchetto: la scrive la finestra, la
 * azzera il server quando qualcuno si collega. */
static bool ap_assenza_detta;

/* --- il thread del server ------------------------------------------------ */

/* La lunghezza annunciata nei quattro byte dell'intestazione, SOLO per il
 * messaggio di registro: la decisione di rifiutare l'ha gia' presa
 * ap_trama_estrai, e non si duplica qui -- due copie della stessa regola sono
 * due regole che un giorno divergono. Si legge byte per byte, come
 * appunti-trama.c la scrive, e per la stessa ragione: su ARM64 il nativo e'
 * little-endian e un memcpy dell'intero darebbe un numero diverso. */
static unsigned long ap_dichiarata(const unsigned char *b)
{
    return ((unsigned long)b[0] << 24) | ((unsigned long)b[1] << 16) |
           ((unsigned long)b[2] << 8) | (unsigned long)b[3];
}

static void ap_chiudi_cliente(void)
{
    if (ap_cliente == INVALID_SOCKET) {
        return;
    }
    EnterCriticalSection(&ap_lucchetto);
    closesocket(ap_cliente);
    ap_cliente = INVALID_SOCKET;
    LeaveCriticalSection(&ap_lucchetto);
    /* I byte a meta' della connessione di prima NON sopravvivono alla
     * connessione: interpretarli insieme a quelli della prossima darebbe una
     * lunghezza composta da due flussi diversi, cioe' spazzatura che sembra una
     * trama. */
    ap_rx_n = 0;
}

static void ap_accetta(void)
{
    SOCKET nuovo = accept(ap_ascolto, NULL, NULL);
    DWORD scadenza = AP_INVIO_MS;

    if (nuovo == INVALID_SOCKET) {
        registro_riga(REG_GUSCIO, "clipboard: accept failed (%d), staying "
                      "in listen", WSAGetLastError());
        return;
    }
    /* UNA CONNESSIONE PER VOLTA, E VINCE L'ULTIMA. Il caso vero e' l'app del
     * guest che si riavvia: la sua connessione di prima puo' restare aperta a
     * lungo dal nostro lato (un guest che sparisce non manda nessun FIN), e
     * tenendo la vecchia il guest nuovo non parlerebbe mai con nessuno --
     * cioe' la riconnessione a scalare, che e' il modo in cui questa funzione
     * si ripara da se', non servirebbe a niente. */
    if (ap_cliente != INVALID_SOCKET) {
        registro_riga(REG_GUSCIO, "clipboard: new connection from the guest, the "
                                  "previous one closes (the last one wins)");
        ap_chiudi_cliente();
    }
    /* Nemmeno la connessione accettata si eredita, per la stessa ragione
     * dell'ascoltatore (vedi appunti_avvia): QEMU si riavvia a ogni riprova, e
     * un socket accettato che finisse dentro l'orfano terrebbe in vita una
     * connessione che dal nostro lato e' chiusa. */
    SetHandleInformation((HANDLE)nuovo, HANDLE_FLAG_INHERIT, 0);
    /* La scadenza in scrittura si mette PRIMA di usare il socket: vedi
     * AP_INVIO_MS per cosa impedisce. */
    setsockopt(nuovo, SOL_SOCKET, SO_SNDTIMEO, (const char *)&scadenza,
               sizeof(scadenza));
    EnterCriticalSection(&ap_lucchetto);
    ap_cliente = nuovo;
    ap_assenza_detta = false;
    LeaveCriticalSection(&ap_lucchetto);
    ap_rx_n = 0;
    registro_riga(REG_GUSCIO, "clipboard: the guest connected");
}

/* Consegna al thread della finestra il testo appena estratto. */
static void ap_consegna_a_windows(const char *testo, size_t n)
{
    HWND h;

    /* GUARDIA 1 DI 2. Se e' identico a cio' che gli abbiamo appena mandato noi,
     * e' l'eco del guest: gli appunti di Windows contengono gia' quel testo, e
     * riscriverlo farebbe scattare WM_CLIPBOARDUPDATE e ripartire l'anello.
     * Si tace, perche' non e' un errore: e' il caso normale ogni volta che il
     * guest riceve qualcosa. */
    if (ap_eco_uguale(&ap_eco_verso_guest, testo, n)) {
        return;
    }
    EnterCriticalSection(&ap_lucchetto);
    memcpy(ap_verso_windows, testo, n);
    ap_verso_windows_n = n;
    /* Se il thread della finestra non ha ancora consumato la consegna
     * precedente, questa la sostituisce: per degli appunti l'ultimo valore e'
     * l'unico che conta, e accodarli mostrerebbe all'utente un testo vecchio. */
    ap_verso_windows_pronto = true;
    /* SI RICORDA PRIMA DI SCRIVERE, non dopo: la scrittura negli appunti fa
     * scattare WM_CLIPBOARDUPDATE, e la notifica puo' arrivare prima che questo
     * thread torni a girare. Ricordando dopo, l'anello avrebbe gia' fatto il
     * primo giro. */
    ap_eco_ricorda(&ap_eco_dal_guest, testo, n);
    LeaveCriticalSection(&ap_lucchetto);

    /* PostMessage e non SendMessage: SendMessage aspetterebbe che il thread
     * della finestra abbia finito, e se quello e' fermo in un dialogo modale
     * (il trascinamento ne apre uno) il server resterebbe fermo con lui. */
    h = finestra_handle();
    if (h) {
        PostMessageA(h, GUSCIO_MSG_APPUNTI_GUEST, 0, 0);
    }
}

static void ap_leggi(void)
{
    int letti = recv(ap_cliente, (char *)ap_rx + ap_rx_n,
                     (int)(sizeof(ap_rx) - ap_rx_n), 0);

    if (letti == 0) {
        registro_riga(REG_GUSCIO, "clipboard: the guest closed the connection");
        ap_chiudi_cliente();
        return;
    }
    if (letti == SOCKET_ERROR) {
        registro_riga(REG_GUSCIO, "clipboard: read failed (%d), the "
                      "connection closes and we go back to listening",
                      WSAGetLastError());
        ap_chiudi_cliente();
        return;
    }
    ap_rx_n += (size_t)letti;

    /* Un ciclo e non un solo tentativo: TCP e' un flusso, e in una sola lettura
     * possono esserci due trame -- succede appena due copie si susseguono in
     * fretta. Fermarsi alla prima lascerebbe la seconda nel buffer fino alla
     * lettura successiva, cioe' consegnerebbe un testo vecchio al prossimo giro.
     *
     * Lo spazio nel buffer non puo' finire: quel che resta e' sempre meno di una
     * trama intera, e una trama intera ci sta per costruzione (vedi ap_rx). */
    for (;;) {
        size_t lung = 0;
        size_t usati = 0;
        int esito = ap_trama_estrai(ap_rx, ap_rx_n, ap_rx_testo,
                                    sizeof(ap_rx_testo), &lung, &usati);

        if (esito == 0) {
            return;   /* servono altri byte: si aspetta la prossima lettura */
        }
        if (esito < 0) {
            /* OLTRE IL TETTO. Si chiude, e si dice QUANTI byte erano: senza il
             * numero, chi legge il registro non sa se ha copiato un file enorme
             * o se il flusso si e' disallineato. L'intestazione c'e' di certo --
             * ap_trama_estrai non puo' rispondere -1 senza averla letta --
             * quindi rileggerla per il messaggio e' sicuro. */
            registro_riga(REG_GUSCIO, "clipboard: the guest announced a "
                          "frame of %lu bytes, over the %lu cap: the "
                          "connection closes (refusing instead of "
                          "truncating)", ap_dichiarata(ap_rx),
                          (unsigned long)AP_TETTO_BYTE);
            ap_chiudi_cliente();
            return;
        }
        ap_consegna_a_windows(ap_rx_testo, lung);
        ap_rx_n -= usati;
        memmove(ap_rx, ap_rx + usati, ap_rx_n);
    }
}

/* Manda al guest cio' che il thread della finestra ha lasciato, se c'e'. */
static void ap_manda_in_attesa(void)
{
    size_t trama;
    size_t quanti;
    int mandati;

    EnterCriticalSection(&ap_lucchetto);
    if (!ap_verso_guest_pronto) {
        LeaveCriticalSection(&ap_lucchetto);
        return;
    }
    ap_verso_guest_pronto = false;
    quanti = ap_verso_guest_n;
    /* La trama si compone QUI, sotto il lucchetto: cosi' il testo condiviso
     * finisce in un buffer che appartiene a questo thread, e la send parte
     * SENZA lucchetto. Tenerlo durante la send bloccherebbe il thread della
     * finestra alla prossima copia per tutta la scadenza dell'invio -- cioe'
     * riporterebbe nel pannello il congelamento che questo thread esiste per
     * evitare. */
    trama = ap_trama_componi(ap_verso_guest, quanti, ap_tx, sizeof(ap_tx));
    /* Si ricorda cio' che si sta per mandare PRIMA di mandarlo, per la stessa
     * ragione dell'altra guardia: l'eco del guest puo' tornare indietro prima
     * che questa funzione finisca. */
    ap_eco_ricorda(&ap_eco_verso_guest, ap_verso_guest, quanti);
    LeaveCriticalSection(&ap_lucchetto);

    if (trama == 0) {
        /* Il tetto e' gia' stato controllato da chi ha riempito il buffer, quindi
         * qui ci si arriva solo se le due regole hanno smesso di concordare. */
        registro_riga(REG_GUSCIO, "clipboard: %lu bytes could not be assembled "
                      "into a frame, not sent", (unsigned long)quanti);
        return;
    }
    if (ap_cliente == INVALID_SOCKET) {
        /* Caduta fra il momento in cui la finestra ha lasciato il testo e adesso.
         * Nessuna riga: la disconnessione l'ha gia' registrata chi l'ha vista. */
        return;
    }
    mandati = send(ap_cliente, (const char *)ap_tx, (int)trama, 0);
    if (mandati == SOCKET_ERROR || (size_t)mandati != trama) {
        /* Anche un invio PARZIALE chiude: su un socket bloccante la send torna
         * solo quando ha preso tutto, quindi qui ci si arriva con la scadenza
         * di AP_INVIO_MS -- e dopo quella non si sa quanti byte siano partiti,
         * cioe' il guest ha in mano mezza intestazione e leggerebbe la prossima
         * lunghezza dal posto sbagliato. */
        registro_riga(REG_GUSCIO, "clipboard: send to the guest failed (%d), the "
                      "connection closes: half a frame would misalign "
                      "every frame after it", WSAGetLastError());
        ap_chiudi_cliente();
        return;
    }
    registro_riga(REG_GUSCIO, "clipboard: %lu bytes sent to the guest",
                  (unsigned long)quanti);
}

static DWORD WINAPI ap_thread(LPVOID arg)
{
    (void)arg;

    /* La lettura del flag passa da Interlocked come la presa del lucchetto in
     * rilascio.c: volatile in C impedisce al compilatore di tenersi il valore in
     * un registro, ma non e' una barriera, e questo flag lo scrive un altro
     * thread. */
    while (InterlockedCompareExchange(&ap_fermare, 0, 0) == 0) {
        fd_set letti;
        struct timeval scadenza;
        int pronti;

        FD_ZERO(&letti);
        FD_SET(ap_ascolto, &letti);
        if (ap_cliente != INVALID_SOCKET) {
            FD_SET(ap_cliente, &letti);
        }
        scadenza.tv_sec = 0;
        scadenza.tv_usec = AP_PASSO_MS * 1000;
        /* Il primo argomento e' ignorato da Winsock (che tiene un vettore di
         * socket, non una maschera di descrittori): si passa 0 come vuole la
         * documentazione, e non un numero calcolato che sarebbe finto. */
        pronti = select(0, &letti, NULL, NULL, &scadenza);
        if (pronti == SOCKET_ERROR) {
            registro_riga(REG_GUSCIO, "clipboard: select failed (%d), the server "
                          "stops. The clipboard no longer syncs, the "
                          "rest of the shell keeps working", WSAGetLastError());
            break;
        }
        if (pronti > 0) {
            /* SI LEGGE PRIMA DI ACCETTARE, e l'ordine conta: se nello stesso
             * giro arrivassero dati sulla connessione vecchia e una connessione
             * nuova, accettare per primo butterebbe via byte gia' arrivati.
             * Cosi' invece la connessione vecchia dice l'ultima cosa che aveva
             * da dire, e solo dopo cede il posto. */
            if (ap_cliente != INVALID_SOCKET && FD_ISSET(ap_cliente, &letti)) {
                ap_leggi();
            }
            if (FD_ISSET(ap_ascolto, &letti)) {
                ap_accetta();
            }
        }
        ap_manda_in_attesa();
    }
    return 0;
}

/* --- avvio e arresto ----------------------------------------------------- */

bool appunti_avvia(int porta)
{
    WSADATA dati;
    struct sockaddr_in indirizzo;
    int errore;

    if (ap_acceso) {
        return true;
    }

    /* LA PORTA E' COMPILATA DENTRO L'APK DEL GUEST, e questa chiave da sola non
     * la cambia.
     *
     * PERCHE' LO DICIAMO invece di lasciarlo scoprire: l'app del guest chiama
     * 10.0.2.2 su una costante (PORTA in ServizioAppunti.java), e se qui si
     * ascolta altrove non si connette. Ma la connessione assente e' un caso
     * NORMALE -- il guscio puo' non esserci affatto, quando la VM si avvia con
     * avvia-android.ps1 -- quindi l'app tace di proposito, e il guasto sarebbe
     * "gli appunti non funzionano" senza una sola riga da nessuna parte.
     *
     * Una manopola che rompe la funzione senza dirlo e' peggio di nessuna
     * manopola: qui almeno lo dice. Cambiare porta richiede di ricostruire
     * l'APK con guest/appunti-guest/build.sh e reinstallarlo. */
    if (porta != AP_PORTA_APK) {
        registro_riga(REG_GUSCIO, "clipboard: porta_appunti=%d but the guest APK "
                      "calls %d, which is compiled into it: without "
                      "rebuilding it with guest/appunti-guest/build.sh and "
                      "reinstalling it, the clipboard will NOT sync and the "
                      "guest will not complain about it", porta, AP_PORTA_APK);
    }

    errore = WSAStartup(MAKEWORD(2, 2), &dati);
    if (errore != 0) {
        registro_riga(REG_GUSCIO, "clipboard: Winsock did not initialise "
                      "(%d), the shared clipboard will not work", errore);
        return false;
    }
    ap_ascolto = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ap_ascolto == INVALID_SOCKET) {
        registro_riga(REG_GUSCIO, "clipboard: socket not created (%d)",
                      WSAGetLastError());
        WSACleanup();
        return false;
    }
    memset(&indirizzo, 0, sizeof(indirizzo));
    indirizzo.sin_family = AF_INET;
    indirizzo.sin_port = htons((unsigned short)porta);
    /* SOLO 127.0.0.1, mai INADDR_ANY: il guest arriva da qui attraverso la rete
     * dell'emulatore (10.0.2.2 e' l'host visto da dentro), quindi non serve
     * altro, e su INADDR_ANY questo ascoltatore sarebbe raggiungibile dalla rete
     * locale. Che resti locale e' comunque un'esposizione dichiarata nella spec:
     * anche qualunque altro processo di questa macchina puo' collegarsi. */
    indirizzo.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    /* IL SOCKET NON SI EREDITA, ed e' la riga che impedisce il guasto piu' caro
     * di questo file.
     *
     * vm.c lancia QEMU con CreateProcessA e bInheritHandles a TRUE (gli serve
     * per il tubo dello stderr). I socket di Winsock sono handle EREDITABILI di
     * nascita, quindi senza questa riga qemu-nostro.exe si ritrova una copia di
     * questo ascoltatore. Finche' tutto va bene non si nota: QEMU non ci fa
     * nulla. Si nota quando il guscio muore male e QEMU resta orfano, perche'
     * la porta resta occupata dalla copia dentro l'orfano.
     *
     * MISURATO, ed e' il motivo per cui la diagnosi era andata altrove:
     * netstat mostra ancora "127.0.0.1:15556 LISTENING" col PID DEL GUSCIO
     * MORTO. Si cerca quel processo, non c'e', e si conclude che la porta e'
     * bloccata da un TIME_WAIT senza padrone. Non lo e': si libera nell'istante
     * in cui si uccide QEMU.
     *
     * NIENTE SO_REUSEADDR. Era la correzione che sembrava ovvia e non ripara
     * nulla, MISURATO su questo Windows in quattro casi:
     *  - TIME_WAIT vero sulla 15556, PID 0, nessun processo vivo -> il bind
     *    riesce lo stesso, senza opzioni. Il commento di prima aveva ragione:
     *    il TIME_WAIT e' delle connessioni accettate, non dell'ascoltatore.
     *  - lo stesso con una connessione a meta' in FIN_WAIT_2.
     *  - con un ascoltatore vivo, SO_REUSEADDR non ruba la porta come temeva il
     *    commento di prima: da 10013, non 10048. Peggiora l'errore invece di
     *    risolvere il caso, perche' 10013 non nomina la porta occupata.
     *  - con l'orfano vivo, entrambi falliscono. La causa era lui. */
    SetHandleInformation((HANDLE)ap_ascolto, HANDLE_FLAG_INHERIT, 0);
    if (bind(ap_ascolto, (struct sockaddr *)&indirizzo,
             sizeof(indirizzo)) == SOCKET_ERROR) {
        registro_riga(REG_GUSCIO, "clipboard: cannot listen on "
                      "127.0.0.1:%d (%d), the port may be taken. The "
                      "shared clipboard stays off, the rest of the shell "
                      "starts", porta, WSAGetLastError());
        closesocket(ap_ascolto);
        ap_ascolto = INVALID_SOCKET;
        WSACleanup();
        return false;
    }
    /* La coda dell'ascolto non e' la regola "una connessione per volta": quella
     * la applica ap_accetta chiudendo la precedente. Qui una coda cortissima
     * farebbe rifiutare il tentativo di un guest che si riconnette proprio
     * mentre stiamo leggendo, e lo costringerebbe ad aspettare il prossimo
     * tentativo a scalare. */
    if (listen(ap_ascolto, SOMAXCONN) == SOCKET_ERROR) {
        registro_riga(REG_GUSCIO, "clipboard: listen failed (%d)",
                      WSAGetLastError());
        closesocket(ap_ascolto);
        ap_ascolto = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    InitializeCriticalSection(&ap_lucchetto);
    ap_fermare = 0;
    ap_acceso = true;
    ap_thread_h = CreateThread(NULL, 0, ap_thread, NULL, 0, NULL);
    if (!ap_thread_h) {
        registro_riga(REG_GUSCIO, "clipboard: the server thread did not start "
                      "(%lu), the shared clipboard stays off",
                      GetLastError());
        ap_acceso = false;
        DeleteCriticalSection(&ap_lucchetto);
        closesocket(ap_ascolto);
        ap_ascolto = INVALID_SOCKET;
        WSACleanup();
        return false;
    }
    registro_riga(REG_GUSCIO, "clipboard: listening on 127.0.0.1:%d", porta);
    return true;
}

bool appunti_ferma(void)
{
    if (!ap_acceso) {
        return true;
    }
    InterlockedExchange(&ap_fermare, 1);
    /* SI ASPETTA il thread invece di lasciarlo andare: scrive nel registro, e
     * chiudere il registro sotto un thread che ci scrive e' una scrittura su un
     * FILE gia' chiuso. L'attesa copre il giro piu' lungo possibile -- un passo
     * della select piu' una send scaduta -- con un margine. */
    if (WaitForSingleObject(ap_thread_h,
                            AP_PASSO_MS + AP_INVIO_MS + 1000) != WAIT_OBJECT_0) {
        /* NON si distrugge niente: il thread e' ancora vivo e possiede i socket
         * e il lucchetto. Distruggere una sezione critica che un altro thread
         * puo' avere in mano e' comportamento non definito, e stiamo comunque
         * uscendo dal processo -- che libera tutto da se'. */
        registro_riga(REG_GUSCIO, "clipboard: the server thread did not "
                                  "stop in time, exiting without waiting for it "
                                  "and without closing the log");
        /* false, e chi chiama salta registro_chiudi: il thread e' vivo e puo'
         * essere dentro registro_riga oltre il controllo "if (!reg_aperto)",
         * che sta FUORI dal lucchetto -- distruggere adesso quella sezione
         * critica e' comportamento non definito. Il registro svuota a ogni
         * riga, quindi non chiuderlo non perde nulla. */
        return false;
    }
    CloseHandle(ap_thread_h);
    ap_thread_h = NULL;
    if (ap_cliente != INVALID_SOCKET) {
        closesocket(ap_cliente);
        ap_cliente = INVALID_SOCKET;
    }
    if (ap_ascolto != INVALID_SOCKET) {
        closesocket(ap_ascolto);
        ap_ascolto = INVALID_SOCKET;
    }
    ap_acceso = false;
    DeleteCriticalSection(&ap_lucchetto);
    WSACleanup();
    return true;
}

/* --- il lato Windows, sempre sul thread della finestra ------------------- */

void appunti_host_cambiati(void)
{
    HANDLE dati;
    const wchar_t *largo;
    int necessari;
    bool collegato;
    bool gia_detto;

    if (!ap_acceso) {
        return;
    }
    /* FORMATO NON TESTUALE: SI IGNORA IN SILENZIO, ed e' una decisione, non una
     * dimenticanza. WM_CLIPBOARDUPDATE arriva a OGNI copia, compresa quella di
     * un'immagine o di un file in Esplora risorse: una riga di registro per
     * ognuna sarebbe rumore che copre le righe che servono.
     *
     * Basta chiedere CF_UNICODETEXT: Windows sintetizza da se' quel formato
     * quando negli appunti c'e' CF_TEXT, quindi controllarli entrambi non
     * troverebbe nulla di piu'. */
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        return;
    }

    EnterCriticalSection(&ap_lucchetto);
    collegato = ap_cliente != INVALID_SOCKET;
    gia_detto = ap_assenza_detta;
    if (!collegato) {
        ap_assenza_detta = true;
    }
    LeaveCriticalSection(&ap_lucchetto);

    if (!collegato) {
        /* Nessuna connessione: una riga e NIENT'ALTRO cambia -- nessun dialogo,
         * gli appunti di Windows restano quelli che l'utente ha appena fatto.
         *
         * UNA riga per periodo di disconnessione, non una per copia: finche' la
         * VM non ha avviato (o l'app del guest non c'e' affatto) ogni Ctrl+C
         * ne produrrebbe una, e sarebbero decine prima che qualcuno si colleghi.
         * Il flag si azzera in ap_accetta, quindi la prossima disconnessione lo
         * dice di nuovo. */
        if (!gia_detto) {
            registro_riga(REG_GUSCIO, "clipboard: no guest connected, what "
                          "you copy here does not reach Android. Not "
                          "repeated until the guest connects");
        }
        return;
    }

    if (!OpenClipboard(finestra_handle())) {
        /* Gli appunti sono una risorsa di sistema che un solo processo per volta
         * puo' aprire: un gestore di appunti che stava leggendo li tiene per
         * qualche millisecondo. NON si riprova in un ciclo: sarebbe un'attesa sul
         * thread della finestra, cioe' il pannello fermo. */
        registro_riga(REG_GUSCIO, "clipboard: another application was holding "
                      "the clipboard (%lu), this copy did not reach the guest",
                      GetLastError());
        return;
    }
    dati = GetClipboardData(CF_UNICODETEXT);
    largo = dati ? (const wchar_t *)GlobalLock(dati) : NULL;
    if (!largo) {
        CloseClipboard();
        return;
    }
    /* Prima si MISURA e poi si converte: cosi' il numero da scrivere nel
     * registro quando si sfonda il tetto e' quello vero, invece di "non ci
     * stava". Il conteggio comprende il terminatore, che la trama non porta. */
    necessari = WideCharToMultiByte(CP_UTF8, 0, largo, -1, NULL, 0, NULL, NULL);
    if (necessari <= 1) {
        /* Appunti vuoti, o una conversione che non e' riuscita. In nessuno dei
         * due casi c'e' un testo da mandare, e mandare zero byte farebbe solo
         * svuotare gli appunti del guest. */
        GlobalUnlock(dati);
        CloseClipboard();
        return;
    }
    if (necessari - 1 > AP_TETTO_BYTE) {
        registro_riga(REG_GUSCIO, "clipboard: the copied text is %lu bytes, "
                      "over the %lu cap: it was NOT sent to the guest (refusing "
                      "instead of truncating, because text cut in "
                      "half is not visible)", (unsigned long)(necessari - 1),
                      (unsigned long)AP_TETTO_BYTE);
        GlobalUnlock(dati);
        CloseClipboard();
        return;
    }

    EnterCriticalSection(&ap_lucchetto);
    WideCharToMultiByte(CP_UTF8, 0, largo, -1, ap_verso_guest, necessari,
                        NULL, NULL);
    /* GUARDIA 2 DI 2. Se e' esattamente l'ultimo testo ARRIVATO dal guest,
     * questa notifica e' l'effetto della nostra stessa scrittura negli appunti:
     * rimandarlo indietro riaprirebbe l'anello. L'unico effetto collaterale e'
     * che ricopiare a mano lo stesso identico testo non lo rispedisce, ed e'
     * invisibile perche' il guest ce l'ha gia'. */
    if (ap_eco_uguale(&ap_eco_dal_guest, ap_verso_guest,
                      (size_t)(necessari - 1))) {
        LeaveCriticalSection(&ap_lucchetto);
        GlobalUnlock(dati);
        CloseClipboard();
        return;
    }
    ap_verso_guest_n = (size_t)(necessari - 1);
    ap_verso_guest_pronto = true;
    LeaveCriticalSection(&ap_lucchetto);

    GlobalUnlock(dati);
    CloseClipboard();
}

void appunti_guest_arrivato(void)
{
    HGLOBAL blocco = NULL;
    wchar_t *dove;
    int caratteri;
    size_t byte = 0;

    if (!ap_acceso) {
        return;
    }
    EnterCriticalSection(&ap_lucchetto);
    if (!ap_verso_windows_pronto) {
        /* Due trame nella stessa lettura mandano due messaggi, e la prima
         * chiamata ha gia' consegnato l'ultimo valore: non e' un errore, e' il
         * prezzo di tenere un solo posto per la consegna invece di una coda. */
        LeaveCriticalSection(&ap_lucchetto);
        return;
    }
    ap_verso_windows_pronto = false;
    byte = ap_verso_windows_n;
    caratteri = MultiByteToWideChar(CP_UTF8, 0, ap_verso_windows, (int)byte,
                                    NULL, 0);
    if (caratteri > 0) {
        /* GMEM_MOVEABLE e non GMEM_FIXED: gli appunti VOGLIONO una maniglia
         * spostabile, e con un blocco fisso SetClipboardData riesce ma cio' che
         * incolla l'altra applicazione e' spazzatura. */
        blocco = GlobalAlloc(GMEM_MOVEABLE,
                             ((size_t)caratteri + 1) * sizeof(wchar_t));
        if (blocco) {
            dove = (wchar_t *)GlobalLock(blocco);
            if (dove) {
                MultiByteToWideChar(CP_UTF8, 0, ap_verso_windows, (int)byte,
                                    dove, caratteri);
                /* Il terminatore lo mettiamo noi: la trama non lo trasporta e
                 * CF_UNICODETEXT lo pretende. Senza, chi incolla legge oltre la
                 * fine finche' non trova due zeri per caso. */
                dove[caratteri] = L'\0';
                GlobalUnlock(blocco);
            } else {
                GlobalFree(blocco);
                blocco = NULL;
            }
        }
    }
    LeaveCriticalSection(&ap_lucchetto);

    if (!blocco) {
        registro_riga(REG_GUSCIO, "clipboard: the %lu bytes from the guest could "
                      "not be prepared for the Windows clipboard",
                      (unsigned long)byte);
        return;
    }
    if (!OpenClipboard(finestra_handle())) {
        registro_riga(REG_GUSCIO, "clipboard: another application was holding "
                      "the clipboard (%lu), the guest text was not "
                      "pasted", GetLastError());
        GlobalFree(blocco);
        return;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, blocco)) {
        registro_riga(REG_GUSCIO, "clipboard: SetClipboardData failed (%lu), the "
                      "guest text was not pasted", GetLastError());
        CloseClipboard();
        /* Solo qui si libera: se SetClipboardData e' riuscita il blocco e' del
         * SISTEMA, e liberarlo sarebbe una doppia liberazione con l'esplosione
         * a distanza in un'altra applicazione che incolla. */
        GlobalFree(blocco);
        return;
    }
    CloseClipboard();
    registro_riga(REG_GUSCIO, "clipboard: %lu bytes from the guest into the "
                  "Windows clipboard", (unsigned long)byte);
}
