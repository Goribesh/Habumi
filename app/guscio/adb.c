/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* adb.c -- il canale verso il guest.
 *
 * PERCHE' E' UN MODULO SUO: e' l'unico pezzo che dipende da un eseguibile di
 * terze parti. Isolarlo rende sostituibile la dipendenza -- oggi adb.exe, un
 * domani un canale nostro via virtio-serial -- senza toccare nient'altro.
 *
 * PERCHE' adb E NON IL MONITOR DI QEMU. Misurato su questa macchina:
 *     sendkey volumeup/home dal monitor  i tasti ARRIVANO al kernel del guest
 *                                        su /dev/input/event0, e Android LI IGNORA
 *     adb shell input keyevent           FUNZIONA: power porta Awake -> Asleep ->
 *                                        Awake, home e back cambiano il fuoco
 *     system_powerdown dal monitor       IGNORATO, Android non ha un demone ACPI
 *     adb shell reboot -p                spegne pulito, QEMU esce da se' in 7 s
 * Perche' il tasto hardware vero venga ignorato non e' noto: il dispositivo e'
 * classificato KEYBOARD | ALPHAKEY con Generic.kl. La misura decide comunque.
 *
 * PERCHE' UN PROCESSO PER COMANDO. Costa 100-300 ms, e su un bottone si sente.
 * L'alternativa -- tenere aperta una sessione "adb shell" e scriverci dentro --
 * e' fuori dalla v1 di proposito: prima si misura se dia noia davvero. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"

/* Il pavimento di velocita' con cui si converte una dimensione in un tempo.
 *
 * MISURATO con dati INCOMPRIMIBILI -- gli zeri davano 1176 MB/s,
 * perche' adb push comprime per default, quindi misuravano la sorgente e non il
 * canale: 115 MB/s su 200 MB a cache calda, 84 su 600 MB, 57 su 200 MB a freddo,
 * 30,8 su 1,2 GB a mano e **27,9 su 1,2 GB attraverso il prodotto**, che e' il
 * piu' lento di tutti e conta piu' degli altri -- l'andatura peggiora col
 * crescere del file (app/misure/pf-misure-preliminari.md). Dieci sta 2,8
 * volte sotto quel valore: e' il margine vero, non le sei volte che si
 * otterrebbero guardando solo i file piccoli. Una scadenza deve perdonare una
 * macchina sotto carico e uccidere solo cio' che e' piantato, quindi il numero si
 * sceglie sempre sul caso peggiore MISURATO -- e se un giorno comparira' un caso
 * piu' lento di 10 MB/s, questo commento e' il posto dove si vede che il margine
 * era 2,8 e non infinito. */
#define ADB_PAVIMENTO_BS (10 * 1024 * 1024)

/* La parte che non dipende dalla dimensione: avvio del processo, stretta di
 * mano col server adb, risposta. E' lo stesso numero della scadenza dei comandi
 * corti, e non e' una coincidenza -- e' lo stesso costo. */
#define ADB_BASE_MS      20000

/* Il costo per FILE, che i byte non catturano.
 *
 * MISURATO : 500 file piccoli spinti come cartella costano 3,44 s,
 * cioe' ~6,9 ms l'uno, mentre i byte non contano niente (50 KB in tutto). Senza
 * questo termine una cartella al tetto di 2000 file avrebbe avuto i soli 20 s di
 * base contro ~14 s misurati: funziona su questa macchina e si rompe su una piu'
 * lenta, con il registro che direbbe "nessuna risposta in 20 s" -- cioe' proprio
 * il guasto confuso che questa scadenza esiste per evitare.
 *
 * Quaranta e' SEI volte i 6,9 ms misurati. Il pavimento sui byte, invece, ha 2,8
 * volte di margine (vedi sopra): i due margini NON sono uguali, e dire che lo
 * fossero era falso. Sono diversi perche' vengono da due misure diverse, e il
 * numero piu' stretto e' quello dei byte, dove l'andatura peggiora col crescere
 * del file. */
#define ADB_PER_FILE_MS  40

/* Tetto: un'ora. Non serve a un file grosso (nel guest ci stanno 4 GB, che al
 * pavimento fanno sette minuti) ma al caso in cui la dimensione che arriva e'
 * sbagliata. */
#define ADB_TETTO_MS     3600000

unsigned adb_scadenza_trasferimento(long long byte, int file)
{
    long long ms = ADB_BASE_MS;

    if (byte <= 0 && file <= 0) {
        return ADB_BASE_MS;
    }
    /* Il confronto viene PRIMA della moltiplicazione, o byte * 1000 sfonderebbe
     * long long su una dimensione assurda -- e una scadenza calcolata su un
     * trabocco sarebbe CORTA, cioe' ucciderebbe un trasferimento sano. */
    if (byte > (long long)ADB_TETTO_MS * ADB_PAVIMENTO_BS / 1000) {
        return ADB_TETTO_MS;
    }
    if (byte > 0) {
        ms += byte * 1000 / ADB_PAVIMENTO_BS;
    }
    if (file > 0) {
        ms += (long long)file * ADB_PER_FILE_MS;
    }
    if (ms > ADB_TETTO_MS) {
        return ADB_TETTO_MS;
    }
    return (unsigned)ms;
}

/* Lancia un comando e cattura lo stdout. Ritorna il codice di uscita, o -1 se
 * il processo non e' partito o non ha risposto entro la scadenza.
 *
 * scadenza_ms: 0 = i 20 s di sempre, cioe' i comandi corti, che sono sincroni
 * sul thread della finestra e quindi devono avere un limite corto. Un valore
 * positivo lo decide il chiamante dalla dimensione del trasferimento, e in quel
 * caso la chiamata gira su un thread: una scadenza lunga non congela niente.
 *
 * PERCHE' NON UNA SCADENZA DI INATTIVITA', che sarebbe meglio: distinguerebbe
 * "lento" da "piantato", ma richiede un segnale di avanzamento e quel segnale
 * NON ESISTE -- misurato, adb non stampa il progresso su un tubo
 * rediretto (600 MB, sette secondi di tubo muto) e l'unico flag che ha (-q)
 * serve a sopprimerlo, non a pretenderlo. */
static int esegui_catturando(const char *riga, char *uscita, int max,
                             unsigned scadenza_ms)
{
    HANDLE leggi = NULL, scrivi = NULL;
    SECURITY_ATTRIBUTES sa = {0};
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    char *copia;
    DWORD codice = (DWORD)-1;
    int scritti = 0;
    ULONGLONG scadenza;
    bool scaduto = false;

    if (uscita && max > 0) {
        uscita[0] = '\0';
    }

    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&leggi, &scrivi, &sa, 0)) {
        return -1;
    }
    /* Il lato di lettura NON va ereditato: se lo fosse, il figlio ne terrebbe
     * una copia e la nostra ReadFile non vedrebbe mai la fine del flusso --
     * si aspetterebbe per sempre un tubo che nessuno chiude. */
    SetHandleInformation(leggi, HANDLE_FLAG_INHERIT, 0);

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;   /* nessuna finestra di console che lampeggia */
    si.hStdOutput = scrivi;
    si.hStdError = scrivi;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    /* CreateProcessA scrive nella riga di comando che riceve: va una copia. */
    copia = _strdup(riga);
    if (!copia) {
        CloseHandle(leggi);
        CloseHandle(scrivi);
        return -1;
    }

    if (!CreateProcessA(NULL, copia, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        registro_riga(REG_GUSCIO, "adb: cannot start (%lu): %s",
                      GetLastError(), riga);
        free(copia);
        CloseHandle(leggi);
        CloseHandle(scrivi);
        return -1;
    }
    /* Il nostro lato di scrittura si chiude subito: finche' resta aperto qui, il
     * tubo non raggiunge mai la fine e la lettura non termina. */
    CloseHandle(scrivi);

    /* Qui prima c'era un ReadFile bloccante senza scadenza: sembrava sicuro
     * perche' la WaitForSingleObject(pi.hProcess, 20000) piu' sotto pareva
     * garantire un limite di 20 s. Non lo garantiva affatto: quella attesa si
     * raggiunge solo DOPO che il ciclo di lettura e' finito, e il ciclo finisce
     * solo quando il tubo arriva a fine flusso, cioe' quando il figlio chiude
     * il proprio stdout (in pratica: quando esce). Un adb.exe impiantato che
     * tiene lo stdout aperto senza scrivere blocca ReadFile per sempre, e la
     * scadenza promessa nel commento non viene mai valutata: l'intero guscio,
     * essendo questa chiamata sincrona sul thread della finestra, si blocca.
     *
     * Il rimedio e' non bloccarsi mai in attesa di byte: si chiede prima a
     * PeekNamedPipe quanti byte ci sono (funziona anche sui tubi anonimi di
     * CreatePipe), si legge SOLO quella quantita' se e' positiva, e altrimenti
     * si controlla a turno se il processo e' uscito o se la scadenza assoluta
     * e' passata, dormendo un poco fra un turno e l'altro per non consumare un
     * core a vuoto. Il controllo dei byte disponibili viene SEMPRE prima del
     * controllo "il processo e' uscito": se il figlio e' uscito lasciando
     * ancora byte nel tubo, quei byte vanno letti prima di dichiarare finito,
     * altrimenti si perderebbe la coda dell'output. */
    scadenza = GetTickCount64() + (scadenza_ms ? scadenza_ms : ADB_BASE_MS);

    for (;;) {
        DWORD disponibili = 0;
        char pezzo[512];
        DWORD letti;

        if (!PeekNamedPipe(leggi, NULL, 0, NULL, &disponibili, NULL)) {
            break;  /* tubo rotto: il figlio ha chiuso lo stdout, fine flusso */
        }

        if (disponibili > 0) {
            DWORD da_leggere = disponibili < sizeof(pezzo)
                                ? disponibili : (DWORD)sizeof(pezzo);

            if (!ReadFile(leggi, pezzo, da_leggere, &letti, NULL) || !letti) {
                break;
            }
            if (uscita && max > 1) {
                int spazio = max - 1 - scritti;

                if (spazio > 0) {
                    DWORD copiare = (DWORD)spazio;

                    if (letti < copiare) {
                        copiare = letti;
                    }
                    memcpy(uscita + scritti, pezzo, copiare);
                    scritti += (int)copiare;
                }
                /* se lo spazio e' esaurito si scarta l'eccedenza ma si CONTINUA
                 * a leggere (niente "break" qui): fermarsi lascerebbe il tubo
                 * pieno non appena l'output supera anche la capacita' del tubo
                 * di sistema, e adb.exe si bloccherebbe in WriteFile in attesa
                 * di un lettore che non torna mai piu' -- lo stesso impianto
                 * che questa funzione deve evitare, causato pero' da noi. */
            }
            continue;
        }

        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            break;  /* niente in coda e il processo e' gia' uscito */
        }

        if (GetTickCount64() >= scadenza) {
            /* I secondi si stampano, non si scrivono a mano: fra i 20 s di un
             * bottone e i minuti di un file da un giga, chi legge il registro
             * deve sapere quale limite e' scattato. */
            registro_riga(REG_GUSCIO,
                          "adb: no answer in %u s, killing the process: %s",
                          (unsigned)((scadenza_ms ? scadenza_ms : ADB_BASE_MS)
                                     / 1000), riga);
            TerminateProcess(pi.hProcess, (UINT)-1);
            scaduto = true;
            break;
        }

        Sleep(10);
    }

    if (uscita && max > 1) {
        uscita[scritti] = '\0';
    }

    if (!scaduto) {
        /* Il tubo puo' essersi rotto (ramo PeekNamedPipe sopra) mentre il
         * processo e' ancora vivo, per esempio se avesse ridiretto altrove il
         * proprio stdout: senza un'attesa qui GetExitCodeProcess rischierebbe
         * di leggere STILL_ACTIVE. Si aspetta solo il tempo che resta alla
         * scadenza, cosi' il limite vale anche in questo caso raro. */
        ULONGLONG adesso = GetTickCount64();
        DWORD rimanente = adesso < scadenza ? (DWORD)(scadenza - adesso) : 0;

        if (WaitForSingleObject(pi.hProcess, rimanente) == WAIT_TIMEOUT) {
            registro_riga(REG_GUSCIO,
                          "adb: no answer in %u s, killing the process: %s",
                          (unsigned)((scadenza_ms ? scadenza_ms : ADB_BASE_MS)
                                     / 1000), riga);
            TerminateProcess(pi.hProcess, (UINT)-1);
            scaduto = true;
        }
    }

    if (scaduto) {
        /* un processo ucciso in silenzio e' indistinguibile da uno che ha
         * risposto no: si aspetta che la terminazione sia effettiva e si
         * ritorna -1, mai il codice di uscita (che dopo TerminateProcess non
         * significa piu' nulla) */
        WaitForSingleObject(pi.hProcess, INFINITE);
        codice = (DWORD)-1;
    } else {
        GetExitCodeProcess(pi.hProcess, &codice);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(leggi);
    free(copia);
    return (int)codice;
}

/* Costruisce in dove il percorso di nome, nella cartella dell'eseguibile
 * corrente. UNICA implementazione: adb_percorso (sotto) e la lettura di
 * config.txt in main.c la condividono, invece di ripetere ciascuno la
 * propria copia di "trova la barra finale in GetModuleFileNameA".
 *
 * IL SINTOMO CHE QUESTA CONDIVISIONE EVITA: senza di lei, main.c leggeva
 * config.txt relativo alla cartella CORRENTE, mentre adb.exe (qui
 * sotto) si cercava accanto all'ESEGUIBILE -- due convenzioni diverse per "il
 * file accanto a me". Le immagini in guest/images/... impongono cwd = radice
 * del progetto, dove config.txt non c'e'; lanciando da runtime/bin/ la
 * configurazione si legge ma mancano le immagini. Non esiste una cwd in cui
 * funzionino entrambe: il file di configurazione che il prodotto SPEDISCE
 * (runtime/bin/config.txt, l'unico che .gitignore salva apposta) non
 * faceva NIENTE, e --config non poteva mostrare altro che i default. */
void guscio_percorso_accanto(const char *nome, char *dove, int max)
{
    char eseguibile[MAX_PATH];
    char *barra;

    GetModuleFileNameA(NULL, eseguibile, sizeof(eseguibile));
    barra = strrchr(eseguibile, '\\');
    if (barra) {
        *(barra + 1) = '\0';
    } else {
        eseguibile[0] = '\0';
    }
    snprintf(dove, max, "%s%s", eseguibile, nome);
}

/* Vedi il commento su questa funzione in guscio.h per il perche' (il
 * Bloccante 3 della revisione finale). Tre tagli all'ultima barra: il primo
 * toglie il nome del file, gli altri due risalgono "bin" e "runtime". */
bool guscio_radice_calcola(const char *eseguibile, char *radice, int max)
{
    char tmp[MAX_PATH];
    char *barra;
    int taglio;

    if (!eseguibile || !radice || max <= 0) {
        return false;
    }
    snprintf(tmp, sizeof(tmp), "%s", eseguibile);
    for (taglio = 0; taglio < 3; taglio++) {
        barra = strrchr(tmp, '\\');
        if (!barra) {
            return false;
        }
        *barra = '\0';
    }
    snprintf(radice, max, "%s", tmp);
    return true;
}

/* Vedi il commento su questa funzione in guscio.h per il perche'. */
bool guscio_radice_verifica(const char *radice, char *provato, int max)
{
    if (!radice || !provato || max <= 0) {
        return false;
    }
    snprintf(provato, max, "%s\\runtime\\bin\\config.txt", radice);
    return GetFileAttributesA(provato) != INVALID_FILE_ATTRIBUTES;
}

/* Vedi il commento su questa funzione in guscio.h per il perche' e per
 * l'ordine obbligato rispetto a registro_apri(). */
bool guscio_radice_imposta(char *radice, int max_radice, char *provato,
                           int max_provato)
{
    char eseguibile[MAX_PATH];

    GetModuleFileNameA(NULL, eseguibile, sizeof(eseguibile));
    if (!guscio_radice_calcola(eseguibile, radice, max_radice)) {
        /* Nessuna radice calcolabile: il percorso cercato, per il messaggio,
         * e' l'eseguibile stesso -- e' tutto cio' che sappiamo. */
        snprintf(provato, max_provato, "%s", eseguibile);
        return false;
    }
    if (!guscio_radice_verifica(radice, provato, max_provato)) {
        return false;
    }
    SetCurrentDirectoryA(radice);
    return true;
}

void adb_percorso(char *dove, int max)
{
    /* adb.exe sta accanto al nostro eseguibile, non nel PATH: il prodotto lo
     * spedisce, e affidarsi al PATH significherebbe usare l'adb di chissa'
     * quale altra installazione, con un protocollo magari diverso. */
    guscio_percorso_accanto("adb.exe", dove, max);
}

/* La riga di comando in una funzione pura, cosi' si prova senza eseguire nulla:
 * e' la parte che puo' sbagliare in silenzio. Le virgolette attorno al percorso
 * di adb non sono ornamento -- adb.exe sta in una cartella con spazi. */
int adb_riga(char *dest, int max, const char *adb, int porta, const char *coda)
{
    int n;

    if (!dest || max <= 0 || !adb || !coda) {
        return 0;
    }
    n = snprintf(dest, (size_t)max, "\"%s\" -s 127.0.0.1:%d %s", adb, porta, coda);
    if (n < 0 || n >= max) {
        dest[0] = '\0';
        return 0;
    }
    return n;
}

/* L'ultima riga non vuota dell'uscita di adb.
 *
 * Sta qui e non in apk.c -- dove stava, col nome apk_ultima_riga -- perche'
 * parla di come adb SCRIVE, non di cosa gli si e' chiesto: la usano
 * l'installazione di un APK e la copia di un file senza sapere l'una
 * dell'altra. Il nome vecchio mentiva su chi fosse il proprietario. */
int adb_ultima_riga(const char *uscita, char *dest, int max)
{
    const char *fine;
    const char *inizio;
    size_t quanti;

    if (!dest || max <= 0) {
        return 0;
    }
    dest[0] = '\0';
    if (!uscita) {
        return 0;
    }

    /* Si cammina dalla FINE: la riga che conta e' l'ultima, e le righe vuote in
     * coda (adb ne lascia) non devono vincere sul messaggio vero. Si tagliano
     * anche i \r perche' adb su Windows chiude con \r\n, e un \r dentro una
     * riga del registro stamperebbe un ritorno a capo in mezzo al testo. */
    fine = uscita + strlen(uscita);
    while (fine > uscita && (fine[-1] == '\n' || fine[-1] == '\r')) {
        fine--;
    }
    if (fine == uscita) {
        return 0;
    }
    inizio = fine;
    while (inizio > uscita && inizio[-1] != '\n' && inizio[-1] != '\r') {
        inizio--;
    }

    quanti = (size_t)(fine - inizio);
    if (quanti > (size_t)max - 1) {
        quanti = (size_t)max - 1;
    }
    memcpy(dest, inizio, quanti);
    dest[quanti] = '\0';
    return (int)quanti;
}

bool adb_esegui_grezzo(const Config *c, const char *coda, char *uscita, int max)
{
    char adb[MAX_PATH];
    char riga[1024];

    adb_percorso(adb, sizeof(adb));
    if (!adb_riga(riga, sizeof(riga), adb, c->porta_adb, coda)) {
        /* Prima di questo controllo una riga troppo lunga veniva TRONCATA da
         * snprintf ed eseguita comunque: un comando diverso da quello chiesto,
         * senza che nessuno lo dicesse. */
        registro_riga(REG_GUSCIO, "adb: command too long, not run");
        return false;
    }
    /* Zero = i 20 s di sempre. Questa e' la via dei comandi corti, che girano
     * sincroni sul thread della finestra: il loro limite deve restare corto. */
    return esegui_catturando(riga, uscita, max, 0) == 0;
}

/* Come adb_esegui_grezzo ma con la scadenza DETTA dal chiamante.
 *
 * La usano push e install, cioe' le due cose che possono durare legittimamente
 * minuti. Un APK grosso sfondava i 20 s assoluti e veniva ucciso a meta'
 * installazione: era un difetto latente del trascinamento APK, e si chiude qui.
 * Una scadenza lunga non congela niente perche' questa via viene chiamata da un
 * thread; se un giorno qualcuno la chiamasse dal wndproc, la finestra
 * resterebbe ferma per tutta la scadenza. */
bool adb_esegui_a_lungo(const Config *c, const char *coda, char *uscita,
                        int max, unsigned scadenza_ms)
{
    char adb[MAX_PATH];
    char riga[1024];

    if (!c || !coda) {
        return false;
    }
    adb_percorso(adb, sizeof(adb));
    if (!adb_riga(riga, sizeof(riga), adb, c->porta_adb, coda)) {
        registro_riga(REG_GUSCIO, "adb: command too long, not run");
        return false;
    }
    return esegui_catturando(riga, uscita, max, scadenza_ms) == 0;
}

/* PERCHE' DELEGA invece di costruire la riga da se': "install" non e' un comando
 * di shell, quindi serviva comunque una variante senza "shell" davanti. Due
 * punti che costruiscono la stessa riga di comando sono due punti che prima o
 * poi divergono. */
bool adb_esegui(const Config *c, const char *comando, char *uscita, int max)
{
    char coda[1024];
    int n = snprintf(coda, sizeof(coda), "shell %s", comando);

    if (n < 0 || n >= (int)sizeof(coda)) {
        return false;
    }
    return adb_esegui_grezzo(c, coda, uscita, max);
}

/* Si collega. adb non si collega da se' a un indirizzo TCP: senza questa
 * chiamata ogni comando risponde "device not found". */
static bool adb_collega(const Config *c)
{
    char adb[MAX_PATH];
    char riga[512];

    adb_percorso(adb, sizeof(adb));
    snprintf(riga, sizeof(riga),
             "\"%s\" connect 127.0.0.1:%d", adb, c->porta_adb);
    return esegui_catturando(riga, NULL, 0, 0) == 0;
}

bool adb_pronto(const Config *c)
{
    char uscita[64];

    if (!adb_esegui(c, "getprop sys.boot_completed", uscita, sizeof(uscita))) {
        adb_collega(c);
        if (!adb_esegui(c, "getprop sys.boot_completed", uscita, sizeof(uscita))) {
            return false;
        }
    }
    return strchr(uscita, '1') != NULL;
}

bool adb_keyevent(const Config *c, const char *keycode)
{
    char comando[128];

    snprintf(comando, sizeof(comando), "input keyevent %s", keycode);
    if (!adb_esegui(c, comando, NULL, 0)) {
        registro_riga(REG_GUSCIO, "adb: %s not delivered", keycode);
        return false;
    }
    registro_riga(REG_GUSCIO, "adb: %s", keycode);
    return true;
}

bool adb_spegni(const Config *c)
{
    registro_riga(REG_GUSCIO, "shutting Android down (reboot -p)");
    return adb_esegui(c, "reboot -p", NULL, 0);
}

bool adb_risoluzione_guest(const Config *c, int *w, int *h)
{
    char uscita[256];
    const char *p;

    if (!adb_esegui(c, "wm size", uscita, sizeof(uscita))) {
        return false;
    }
    /* "Physical size: 1280x800" */
    p = strchr(uscita, ':');
    if (!p) {
        return false;
    }
    return sscanf(p + 1, "%dx%d", w, h) == 2;
}
