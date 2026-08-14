/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-ciclo.c -- il lato CICLO PRINCIPALE del backend -display winq.
 *
 * La divisione fra questo file e winq-window.c e' il confine fra i due thread,
 * ed e' scritta nei FILE invece che in un commento perche' un commento non si
 * puo' violare per sbaglio:
 *
 *     winq-window.c   il thread della finestra. Win32, wndproc, traduzione dei
 *                     messaggi in record. Non chiama niente di QEMU.
 *     winq-ciclo.c    questo file. QEMU: il DisplayChangeListener, il
 *                     drenaggio della coda, il battito, la presentazione a
 *                     cadenza, il tubo dei comandi del guscio, l'annuncio
 *                     della frequenza, la risoluzione. Gira SEMPRE sul ciclo
 *                     principale, quindi puo' prendere il BQL e chiamare dpy_*.
 *
 * PERCHE' LA DIVISIONE. Un ciclo modale di Windows fermava la macchina
 * virtuale -- 23.848 ms MISURATI tenendo il bordo, contro zero
 * buchi a riposo -- perche' la pompa dei messaggi viveva dentro un timer del
 * ciclo principale.
 *
 * QUESTO COMMIT NON CAMBIA NIENTE, SPOSTA SOLTANTO: il thread arriva nel commit
 * successivo. Separarli serve a poter leggere il diff dello scorporo senza che
 * sia sepolto sotto novecento righe che hanno solo cambiato file.
 */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"   /* qemu_bh_new, per la sveglia dal thread */
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-control.h"
#include "qemu/error-report.h"
#include "system/system.h"
#include "ui/console.h"
#include "winq.h"
#include "winq-coda.h"

/* I TRE NUMERI DEL TOCCO DEVONO COMBACIARE.
 *
 * winq-coda.h ricopia i valori di InputMultiTouchType invece di includere QEMU,
 * perche' includerlo ucciderebbe la proprieta' per cui quel modulo si prova da
 * solo, senza l'albero di QEMU. Il rischio che qualcuno rinumeri quell'enum e
 * nessuno se ne accorga si chiude qui: se divergono, la compilazione si ferma.
 * Un tocco tradotto col numero sbagliato darebbe un BEGIN al posto di un END,
 * cioe' un dito che resta appoggiato per sempre. */
QEMU_BUILD_BUG_ON(WINQ_TOCCO_BEGIN  != INPUT_MULTI_TOUCH_TYPE_BEGIN);
QEMU_BUILD_BUG_ON(WINQ_TOCCO_UPDATE != INPUT_MULTI_TOUCH_TYPE_UPDATE);
QEMU_BUILD_BUG_ON(WINQ_TOCCO_END    != INPUT_MULTI_TOUCH_TYPE_END);

static QEMUTimer *winq_scadenza;

static void winq_scadenza_scattata(void *opaque)
{
    /* IL GUSCIO NON HA RISPOSTO. Puo' essere morto fra il segnale e lo
     * spegnimento, o adb puo' non aver raggiunto il guest. In entrambi i casi
     * restare vivi senza finestra e' il guasto peggiore, quindi si esce come si
     * usciva prima -- dichiarandolo, perche' una chiusura forzata silenziosa
     * sarebbe indistinguibile da quella di ieri. */
    error_report("winq: the shell did not shut Android down within 30 s: exiting "
                 "anyway. The data partition may not have been closed "
                 "cleanly.");
    qmp_quit(NULL);
}

void winq_arma_scadenza_chiusura(void)
{
    if (winq_scadenza) {
        return;
    }
    winq_scadenza = timer_new_ms(QEMU_CLOCK_REALTIME, winq_scadenza_scattata,
                                 NULL);
    timer_mod(winq_scadenza, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 30000);
}
/* IL TUBO DEI COMANDI DAL GUSCIO.
 *
 * Un solo comando, testuale, terminato da newline:
 *     risoluzione <larghezza> <altezza>
 *
 * PERCHE' LETTO DALLA POMPA E NON DA UN THREAD: la texture del guest vive nel
 * contesto GL del thread principale, e dpy_set_ui_info va chiamata da li'.
 * Condividere il contesto fra thread e' il primo rischio che la spec di
 * host-window vieta.
 *
 * PERCHE' PeekNamedPipe PRIMA DI ReadFile: una lettura bloccante fermerebbe la
 * pompa ogni volta che nessuno scrive -- cioe' quasi sempre -- e la finestra di
 * Android si inchioderebbe. E' anche la ragione per cui il comando e' testuale
 * con newline: un formato a lunghezza fissa costringerebbe a ricomporre letture
 * parziali, che e' il codice che si scrive male leggendo senza bloccare. */
static HANDLE winq_tubo = INVALID_HANDLE_VALUE;

static void winq_tubo_apri(void)
{
    winq_tubo = CreateNamedPipeA("\\\\.\\pipe\\AndroidRuntimeGuscio.comandi",
                                 PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
                                 PIPE_NOWAIT,
                                 1, 0, 4096, 0, NULL);
    if (winq_tubo == INVALID_HANDLE_VALUE) {
        info_report("winq: no command pipe (%lu): rotation from the "
                    "shell will not work", GetLastError());
    }
}

static void winq_tubo_esegui(const char *comando)
{
    int w = 0, h = 0;

    /* "mappa carica <percorso>" e "mappa impara <...>": il resto della riga,
     * comando+13, e' passato PARI PARI -- winq_tubo_passo (sopra) ha gia'
     * tolto sia il \n di terminazione (mai copiato in accumulo) sia ogni \r,
     * esattamente come fa gia' per "risoluzione" qui sotto, quindi non c'e'
     * niente da togliere una seconda volta.
     *
     * g_strdup qui, g_free nel wndproc (winq-window.c): la proprieta' della
     * stringa passa col messaggio postato, e va rispettata anche se il
     * wndproc la rifiuta -- vedi il commento su WINQ_MSG_MAPPA_CARICA in
     * winq.h. SI POSTA E NON SI MANDA, per la stessa ragione di
     * WINQ_MSG_DIMENSIONA: un SendMessage da questo thread (il ciclo
     * principale) al thread della finestra riapre esattamente il blocco che
     * lo scorporo in due thread e' stato fatto per chiudere. */
    if (!strncmp(comando, "mappa carica ", 13)) {
        PostMessageA(winq.hwnd, WINQ_MSG_MAPPA_CARICA, 0,
                     (LPARAM)g_strdup(comando + 13));
        return;
    }
    if (!strncmp(comando, "mappa impara ", 13)) {
        PostMessageA(winq.hwnd, WINQ_MSG_MAPPA_IMPARA, 0,
                     (LPARAM)g_strdup(comando + 13));
        return;
    }
    if (!strcmp(comando, "mappa spegni")) {
        PostMessageA(winq.hwnd, WINQ_MSG_MAPPA_SPEGNI, 0, 0);
        return;
    }

    if (sscanf(comando, "risoluzione %d %d", &w, &h) == 2) {
        /* Si valida invece di girare al device qualunque cosa: con larghezza o
         * altezza a ZERO virtio-gpu DISABILITA l'uscita
         * (hw/display/virtio-gpu-base.c:116-120), cioe' lo schermo si spegne e
         * il comando sembra "having done nothing". */
        if (w < 320 || h < 320 || w > 8192 || h > 8192) {
            info_report("winq: resolution %dx%d refused, outside 320..8192",
                        w, h);
            return;
        }
        winq_imposta_risoluzione(w, h);
        return;
    }
    info_report("winq: unrecognised command on the pipe: %s", comando);
}

/* Chiude la connessione lato server con questo client e rimette subito
 * l'unica istanza in ascolto per il prossimo. QUESTA CHIAMATA MANCAVA, ed
 * era il difetto critico trovato dalla revisione: guscio/tubo.c apre, scrive
 * e chiude un handle client a ogni comando (scelta motivata li'), ma quando
 * il client chiude, l'istanza SERVER resta CONNESSA a un client che non c'e'
 * piu' finche' qualcuno non chiama DisconnectNamedPipe. Con nMaxInstances = 1
 * la prossima CreateFileA lato guscio trova quindi tutte le istanze occupate
 * e fallisce con ERROR_PIPE_BUSY (231): dalla seconda pressione di "ruota" in
 * poi il tubo e' morto. Richiamare ConnectNamedPipe da sola (come faceva
 * prima questa funzione, a ogni giro della pompa) NON basta: senza
 * DisconnectNamedPipe prima, l'istanza resta appesa al client sparito e
 * ConnectNamedPipe non la libera.
 *
 * Il difetto restava INVISIBILE perche' il ripiego adb (settings put system
 * user_rotation) fa ruotare lo schermo per conto suo comunque: la rotazione
 * si vedeva, il tubo era morto, e nessuno se ne accorgeva senza guardare il
 * pannello del guscio o il log di QEMU. */
static void winq_tubo_riarma(void)
{
    DisconnectNamedPipe(winq_tubo);
    ConnectNamedPipe(winq_tubo, NULL);   /* con PIPE_NOWAIT non blocca */
}

static void winq_tubo_passo(void)
{
    static char accumulo[512];
    static int quanti;
    DWORD disponibili = 0;
    char pezzo[256];
    DWORD letti;
    DWORD i;

    if (winq_tubo == INVALID_HANDLE_VALUE) {
        return;
    }
    ConnectNamedPipe(winq_tubo, NULL);   /* con PIPE_NOWAIT non blocca */
    if (!PeekNamedPipe(winq_tubo, NULL, 0, NULL, &disponibili, NULL)) {
        /* ERROR_BROKEN_PIPE: il client ha chiuso senza che restasse altro da
         * leggere. Senza riarmare anche qui, l'istanza resta appesa esattamente
         * come nel caso normale gestito piu' sotto, e si resterebbe bloccati
         * al prossimo comando. */
        if (GetLastError() == ERROR_BROKEN_PIPE) {
            winq_tubo_riarma();
        }
        return;
    }
    if (!disponibili) {
        return;
    }
    if (!ReadFile(winq_tubo, pezzo, sizeof(pezzo), &letti, NULL) || !letti) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
            winq_tubo_riarma();
        }
        return;
    }
    for (i = 0; i < letti; i++) {
        if (pezzo[i] == '\n' || quanti >= (int)sizeof(accumulo) - 1) {
            accumulo[quanti] = '\0';
            if (quanti) {
                winq_tubo_esegui(accumulo);
            }
            quanti = 0;
            /* Il comando e' completo: guscio/tubo.c apre, scrive e chiude a
             * ogni comando, quindi a questo punto il client e' gia' sparito o
             * sta sparendo. Si disconnette e si riarma SUBITO, prima che il
             * prossimo giro della pompa possa accettare un nuovo client: vedi
             * il commento su winq_tubo_riarma per il perche' della chiamata a
             * DisconnectNamedPipe. */
            winq_tubo_riarma();
        } else if (pezzo[i] != '\r') {
            accumulo[quanti++] = pezzo[i];
        }
    }
}
/* IL DRENAGGIO: l'unico posto in cui i record della finestra diventano chiamate
 * a QEMU.
 *
 * Gira SEMPRE sul ciclo principale -- sia quando lo chiama la mezza sveglia
 * (la bottom half qui sotto), sia quando lo chiama il battito, sia dal
 * .dpy_refresh -- quindi il BQL c'e' gia' e non si prende niente.
 *
 * Lo stato degli slot del tocco vive qui e non attraversa mai il confine: e'
 * questa funzione a chiamare winq_slot_per_pointer, con l'identificativo che il
 * record porta cosi' com'e' arrivato da Windows. */
static void winq_coda_drena(void)
{
    static unsigned long scartati_detti;
    unsigned long scartati;
    WinqRec r;

    while (winq_coda_prendi(&r)) {
        switch (r.tipo) {
        case WINQ_REC_PUNTATORE:
            /* pos_valida falsa: le coordinate sono ignorate da
             * winq_handle_pointer_event, che riusa l'ultima posizione nota
             * dello slot -- e' il ramo con GetPointerInfo fallita e quello di
             * WM_(POINTER)CAPTURECHANGED, dove per un rilascio la posizione non
             * serve. Si passa zero invece di lasciar passare quello che c'e'
             * nel record, perche' un giorno qualcuno guardera' questi numeri in
             * un log e devono dire "was not there" e non un valore plausibile. */
            winq_handle_pointer_event(
                winq_slot_per_pointer(r.id, (InputMultiTouchType)r.tocco),
                r.pos_valida ? r.x : 0, r.pos_valida ? r.y : 0,
                (InputMultiTouchType)r.tocco);
            break;
        case WINQ_REC_TASTO:
            winq_key((WPARAM)r.vk, (LPARAM)r.info, r.premuto);
            break;
        case WINQ_REC_ROTELLA:
            winq_rotella(r.delta);
            break;
        case WINQ_REC_DIMENSIONE:
            winq.geom.win_w = r.w;
            winq.geom.win_h = r.h;
#ifdef CONFIG_OPENGL
            /* Comincia una convergenza NUOVA della catena di scambio: senza
             * questo il limite di tentativi si esaurisce una volta per processo
             * invece che una per ridimensionamento. */
            winq_present_nuova_dimensione();
#endif
            break;
        case WINQ_REC_ESPONI:
            winq.expose = true;
            break;
        case WINQ_REC_CHIUSURA:
            if (r.inatteso) {
                error_report("winq: the window message loop has "
                             "ended by itself: exiting, because a virtual "
                             "machine alive without a window can only be closed "
                             "from Task Manager");
                qmp_quit(NULL);
            } else if (r.guscio_ha_risposto) {
                winq_arma_scadenza_chiusura();
            } else {
                qmp_quit(NULL);
            }
            break;
        default:
            /* Non si tace: un tipo sconosciuto vuol dire che il produttore e il
             * consumatore non sono piu' d'accordo, cioe' un difetto di
             * compilazione travestito da dato. */
            warn_report_once("winq: unknown record type %d in the queue",
                             (int)r.tipo);
            break;
        }
    }

    /* Gli scarti si riportano quando CAMBIANO, non a ogni giro: a ogni giro
     * sarebbero centinaia di righe identiche al secondo. */
    scartati = winq_coda_scartati();
    if (scartati != scartati_detti) {
        warn_report("winq: %lu window events dropped in total, the "
                    "queue filled up", scartati);
        scartati_detti = scartati;
    }
}

/* La sveglia dal thread della finestra.
 *
 * PERCHE' UNA BOTTOM HALF E NON ASPETTARE IL BATTITO: il battito gira ogni
 * cinque millisecondi, e un evento che aspetta fino a cinque millisecondi e' un
 * evento in ritardo per niente. qemu_bh_schedule e' il modo con cui in QEMU un
 * thread qualunque chiede al ciclo principale di fare qualcosa appena puo'
 * (include/qemu/aio.h): il precedente si copia, non si inventa. */
static QEMUBH *winq_bh;

static void winq_bh_corpo(void *opaque)
{
    winq_coda_drena();
}

void winq_sveglia(void)
{
    /* Puo' arrivare prima che winq_init abbia creato la bottom half: il
     * wndproc accoda gia' dentro CreateWindowEx. Il record non si perde --
     * resta in coda e lo prende il primo drenaggio. */
    if (winq_bh) {
        qemu_bh_schedule(winq_bh);
    }
}

/* .dpy_refresh. La coda Win32 si svuota PER PRIMA, e questo e' l'unico punto in
 * cui ci si scosta dall'ordine di sdl2_gl_refresh (ui/sdl2-gl.c:112-124), che
 * la svuota per ultima.
 *
 * Il ragionamento ovvio -- "draining it last avoids one frame of delay" -- e'
 * l'opposto del vero, e vale scriverlo perche' e' stato scritto sbagliato una
 * volta. WM_SIZE e WM_PAINT alzano winq.expose, e quel flag viene consumato da
 * winq_present_frame: se la coda si svuota DOPO la presentazione, il flag alzato
 * adesso viene visto solo al giro successivo, cioe' un
 * GUI_REFRESH_INTERVAL_DEFAULT dopo (~30 ms).
 *
 * Per SDL l'ordine inverso non produce ritardo, ma non per merito dell'ordine:
 * SDL_WINDOWEVENT_RESIZED/EXPOSED chiamano sdl2_redraw() IMMEDIATAMENTE dentro
 * sdl2_poll_events (ui/sdl2.c:603-614), senza flag differito. Copiare l'ordine
 * di SDL senza copiarne il meccanismo lasciava quindi i 30 ms.
 *
 * Svuotare per prima serve anche al tocco, e questa e' la ragione per cui il
 * riordino e' avvenuto nel task del tocco: i WM_POINTERUPDATE accodati vengono
 * tradotti e messi in coda a virtio PRIMA che graphic_hw_update chieda al guest
 * il frame nuovo, quindi il frame che il guest produce in questo giro puo' gia'
 * tenere conto del dito. Con l'ordine precedente il dito veniva sempre letto un
 * frame dopo l'immagine che lo mostra.
 *
 * Nessun effetto negativo misurato (la verifica di allora): l'ordine non
 * introduce ricorsione, perche' winq_pump_messages non chiama ne'
 * graphic_hw_update ne' winq_present_frame -- WM_SIZE e WM_PAINT alzano solo un
 * flag. */
/* IL BATTITO RAPIDO DELL'INPUT.
 *
 * PERCHE' ESISTE. QEMU chiama .dpy_refresh dal proprio timer generico, ogni
 * GUI_REFRESH_INTERVAL_DEFAULT = 30 ms (ui/console.c:47, 874-883). Trenta
 * millisecondi vanno benissimo per ridisegnare uno schermo, e sono troppi per
 * l'input: il digitalizzatore del Surface produce a 120 Hz o piu', quindi si
 * campiona un evento su quattro.
 *
 * Peggio del campionamento e' il RAGGRUPPAMENTO. Svuotando la coda Win32 una
 * volta ogni 30 ms, tutti i WM_POINTERUPDATE accumulati vengono tradotti e
 * consegnati a virtio nello stesso istante. MISURATO con getevent nel guest, sui
 * SYN_REPORT consecutivi di un dito reale:
 *
 *     +35.3  +38.4  +28.4  +22.4  +27.8 ms     <-- il ciclo di refresh
 *     +0.1   +0.0   +0.0   +0.0   +0.0  ms     <-- raffiche nello stesso istante
 *
 * Android calcola la velocita' del dito dai tempi fra i campioni: con campioni a
 * distanza zero la velocita' e' indeterminata, e senza velocita' non c'e' inerzia.
 *
 * PERCHE' UN TIMER E NON UN THREAD. La texture del guest vive nel contesto GL del
 * thread principale, e presentarla da un altro thread vorrebbe dire condividerlo
 * -- il primo rischio che la spec vieta. timer_new_ms(QEMU_CLOCK_REALTIME, ...)
 * gira sul ciclo principale, ed e' lo stesso meccanismo con cui QEMU arma
 * gui_update in gui_setup_refresh (ui/console.c:108-124): il precedente si copia,
 * non si inventa.
 *
 * PERCHE' IL BATTITO PRESENTA ANCORA, MA PASSANDO DA UNA STROZZATURA. Un frame
 * prodotto dal guest deve vedersi entro il periodo del battito (5 ms) invece
 * che entro i 30 ms del refresh di QEMU, o un ridimensionamento
 * convergerebbe troppo lento. Ma chiamare winq_present_frame() a ogni giro
 * del battito NON e' innocuo come sembrava all'inizio: sono fino a 200
 * presentazioni al secondo su un compositore che ne mostra al massimo 120,
 * sullo stesso thread principale che serve virtio -- MISURATO che questo
 * costa latenza vera sul transito virtio, non solo il confronto di guardia
 * che winq_present_frame fa quando non c'e' nulla di nuovo. Il rimedio e'
 * winq_presenta_a_cadenza (piu' sotto in questo file), che tiene la
 * presentazione alla cadenza del pannello senza rallentare questo battito:
 * vedi il commento sopra quella funzione per la misura per esteso.
 *
 * Il periodo si regola con WINQ_PERIODO_MS per poter MISURARE l'effetto invece di
 * sceglierlo a intuito. Il valore preso da 1 a 30 ms; il default 5 sta sotto il
 * periodo del digitalizzatore a 120 Hz (8,3 ms) senza scendere a un valore dove il
 * lavoro del timer stesso diventa il costo dominante. */
static QEMUTimer *winq_battito;
static int winq_periodo_ms;

static int winq_leggi_periodo(void)
{
    const char *v = getenv("WINQ_PERIODO_MS");
    int ms;

    if (!v || !*v) {
        return 5;
    }
    ms = atoi(v);
    if (ms < 1) {
        ms = 1;
    } else if (ms > 30) {
        /* Oltre i 30 ms non ha senso: e' il periodo del refresh di QEMU, che
         * pompa comunque. */
        ms = 30;
    }
    return ms;
}

/* LA STROZZATURA DELLA PRESENTAZIONE, e perche' non e' lo stesso timer del
 * battito.
 *
 * IL PROBLEMA MISURATO. Fino a questo lavoro winq_tick chiamava
 * winq_present_frame() a ogni giro, cioe' fino a 200 presentazioni al secondo
 * su un compositore che ne mostra al massimo 120 (hz=120 spedito, guest a
 * 2560x1600), sullo stesso thread principale che serve virtio. La diagnosi:
 * lavoro dell'app nel guest 0,18 ms per frame (input+animazione+layout+
 * draw+upload sommati), GPU 7 ms, ma "issue draw commands" 9,22 ms di attesa
 * sul transito virtio e 5,60 ms di attesa per un buffer libero, con la CPU
 * del guest sotto carico al 449% idle su 600% (119% sys contro 26% user). Il
 * guest non e' limitato ne' dalla CPU ne' dalla GPU: paga una latenza fissa
 * sul transito virtio a ogni frame, e ogni millisecondo speso a presentare e'
 * un millisecondo in cui il guest non viene servito.
 *
 * LA MISURA, fatta INTERLACCIANDO i bracci (5, 8, 16, 5, 8, 16) invece di
 * metterli in fila. L'interlacciamento non e' pignoleria: una prima serie messa
 * in fila era stata invalidata dalla deriva della macchina -- quattro esecuzioni
 * consecutive della STESSA configurazione avevano reso 6,19% e 23,15% di janky,
 * peggiorando nell'ordine di AVVIO invece che con la variabile. Alternando, la
 * deriva colpisce tutti i bracci allo stesso modo.
 *
 *   intervallo        tornata 1   tornata 2
 *   5 ms (di prima)     11,78%      19,93%
 *   8 ms (da hz=120)    10,37%       6,23%     <- il piu' basso in ENTRAMBE
 *   16 ms               12,98%      17,88%
 *
 * QUANTO VALE, detto onestamente. Il verso e' coerente -- 8 ms vince due volte
 * su due, e con esso la mediana del frame (17 e 13 ms contro 19-21) -- ma
 * l'ENTITA' no: nella tornata 1 i tre bracci stanno dentro 2,6 punti, cioe' sono
 * indistinguibili, mentre nella 2 si aprono da 6% a 20%. Con tre bracci, vincere
 * due volte di fila ha circa l'11% di probabilita' di essere caso. E' un indizio
 * che regge, non una dimostrazione.
 *
 * COSA QUESTA MISURA HA SMENTITO, e vale piu' di cio' che ha confermato: una
 * coppia singola precedente dava 5 ms -> 16 ms come 34,40% -> 14,61%, e quel
 * numero era stato scritto qui. Falso: 16 ms e' peggio di 8 in entrambe le
 * tornate e non batte 5. Era rumore. Se qualcuno rimisura e trova altro, creda
 * alla serie interlacciata e non a una coppia.
 *
 * Cio' che regge indipendentemente dai numeri e' il ragionamento: 200
 * presentazioni al secondo verso un compositore che ne mostra 120, sul thread
 * che serve virtio, sono lavoro buttato a prescindere da quanto renda toglierlo.
 * E il valore migliore risulta essere proprio quello che il codice ricava da
 * se' dall'hz annunciato, che e' la ragione per cui lo ricava invece di fissarlo.
 *
 * NON e' la correzione giusta: WINQ_PERIODO_MS governa un solo timer che fa
 * due cose, e i 5 ms di quel timer sono stati scelti apposta per il
 * digitalizzatore a 120 Hz (vedi il commento sopra winq_leggi_periodo) --
 * rallentarli peggiorerebbe i 2015 eventi di input in ritardo su 1691 frame
 * gia' misurati. Servono due cadenze indipendenti: l'input pompato a ogni
 * battito come oggi, la presentazione limitata alla cadenza del pannello.
 *
 * PERCHE' NON SALTA NULLA. winq_present_frame() esce subito se non c'e'
 * niente di nuovo (winq_updates, winq.expose): questa strozzatura non
 * sceglie COSA presentare, sceglie solo QUANDO provarci. Un aggiornamento non
 * consumato resta pendente e viene preso al primo tentativo utile, al
 * massimo winq_presenta_intervallo_ms dopo -- non piu' tardi, perche'
 * winq_tick continua a provare a ogni winq_periodo_ms (5 ms di default),
 * molto piu' spesso dell'intervallo che si vuole imporre. Un expose o un
 * ridimensionamento restano quindi appesi al massimo un intervallo, mai di
 * piu'.
 *
 * DUE CHIAMANTI, UN SOLO CANCELLO. winq_present_frame() era chiamata sia da
 * winq_tick sia da winq_display_refresh: bastava lasciarne una senza il
 * cancello per vanificarlo. Entrambe ora chiamano solo
 * winq_presenta_a_cadenza(), mai winq_present_frame() direttamente (nessun
 * altro chiamante nel file). */
static int64_t winq_presenta_ultima_ms = -1;   /* -1: nessuna presentazione ancora fatta */
static int winq_presenta_intervallo_ms = 16;   /* ripiego finche' l'hz non e' noto */
static int winq_presenta_ms_forzato;           /* da WINQ_PRESENTA_MS, 0 = non forzato */

/* WINQ_PRESENTA_MS, nello spirito di WINQ_PERIODO_MS e WINQ_HZ: forza
 * l'intervallo minimo fra due presentazioni per poter confrontare 8 ms
 * contro 16 SENZA ricompilare, invece di scegliere a intuito. Letta una
 * sola volta in winq_init, come winq_leggi_periodo: il valore non deve
 * cambiare a meta' misura. Limite 1..100 ms: sotto 1 ms non e' piu' una
 * strozzatura, sopra 100 ms (10 Hz) non misura piu' niente di realistico
 * per un compositore. */
static int winq_presenta_leggi_forzato_ms(void)
{
    const char *v = getenv("WINQ_PRESENTA_MS");
    int ms;

    if (!v || !*v) {
        return 0;
    }
    ms = atoi(v);
    if (ms < 1) {
        ms = 1;
    } else if (ms > 100) {
        ms = 100;
    }
    return ms;
}

/* Da hz annunciato al guest a intervallo minimo fra due presentazioni. hz <= 1
 * copre sia -1 (GetDC sulla finestra fallita) sia 0/1 (il pannello non
 * dichiara una frequenza utile, vedi winq_hz_da_annunciare): in entrambi i
 * casi non si conosce la cadenza del compositore, e 16 ms (60 Hz) e' il
 * ripiego sicuro. Altrimenti 1000/hz: 120 Hz -> 8 ms, 60 Hz -> 16 ms,
 * coerente con la tabella qui sopra.
 *
 * IL MINIMO A 1 ms NON E' PARANOIA. WINQ_HZ e' letta con atoi() e non ha un
 * tetto (vedi winq_hz_da_annunciare), quindi un refuso di una cifra --
 * WINQ_HZ=1200 invece di 120, che sulla tastiera e' un tasto -- darebbe
 * 1000/1200 = 0 per troncamento intero. Un intervallo di zero DISATTIVA la
 * strozzatura in silenzio: si torna a presentare a ogni battito, cioe' al
 * comportamento che questo codice esiste per togliere, senza una riga di log
 * che lo dica. E' lo stesso limite minimo che winq_presenta_leggi_forzato_ms
 * impone al valore forzato, per la stessa ragione. */
static int winq_presenta_da_hz(int hz)
{
    int ms;

    if (hz <= 1) {
        return 16;
    }
    ms = 1000 / hz;
    return ms > 0 ? ms : 1;
}

/* Ricalcola winq_presenta_intervallo_ms SOLO quando ha senso: quando la
 * frequenza annunciata al guest cambia, non a ogni battito. Chiamata dai due
 * soli punti che gia' invocano winq_hz_da_annunciare() (winq_annuncia_frequenza
 * al cambio di scanout, winq_imposta_risoluzione alla rotazione), con l'hz che
 * hanno gia' ottenuto loro: questa funzione non chiama MAI GetDC/GetDeviceCaps
 * da sola, o sarebbero 200 chiamate al secondo dal battito invece delle poche
 * volte in cui la frequenza puo' davvero cambiare. Non e' una terza fonte di
 * verita' sulla frequenza: winq_hz_da_annunciare resta l'unica decisione,
 * questa funzione la traduce solo in un intervallo.
 *
 * WINQ_PRESENTA_MS, se impostata, vince sempre: la misura in corso non deve
 * essere disturbata da un ricalcolo automatico. */
static void winq_presenta_ricalcola_intervallo(int hz)
{
    static uint32_t hz_visto;
    static bool hz_visto_mai;
    int nuovo;

    if (winq_presenta_ms_forzato) {
        return;
    }
    if (hz_visto_mai && hz_visto == (uint32_t)hz) {
        return;   /* stesso hz di prima, l'intervallo non cambia */
    }
    hz_visto_mai = true;
    hz_visto = (uint32_t)hz;

    nuovo = winq_presenta_da_hz(hz);
    if (nuovo != winq_presenta_intervallo_ms) {
        winq_presenta_intervallo_ms = nuovo;
        info_report("winq: minimum interval between two presentations %d ms "
                    "(hz=%d)", nuovo, hz);
    }
}

/* IL CANCELLO UNICO: chiamata da winq_tick e da winq_display_refresh al posto
 * di winq_present_frame() diretta, cosi' nessuna delle due presenta piu'
 * spesso di winq_presenta_intervallo_ms. Non salta un aggiornamento: si
 * limita a non riprovare prima che l'intervallo sia passato, e
 * winq_present_frame() stessa esce subito se non c'e' niente di nuovo (vedi
 * il commento sopra questo blocco). */
static void winq_presenta_a_cadenza(void)
{
    int64_t adesso = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    if (winq_presenta_ultima_ms >= 0 &&
        adesso - winq_presenta_ultima_ms < winq_presenta_intervallo_ms) {
        return;
    }
    winq_presenta_ultima_ms = adesso;
#ifdef CONFIG_OPENGL
    winq_present_frame();
#endif
}

/* WINQ_TRACCIA_AVVIO=1: briciole sui primi giri del battito.
 *
 * PERCHE' ESISTE. Con una opengl32.dll costruita in casa (Mesa 26.2.0-rc3 da
 * research/mesa-src) QEMU esce con codice 87 subito DOPO winq_init, senza
 * scrivere nulla -- e succede anche con virtio-gpu-pci, cioe' senza virgl.
 * L'ultimo messaggio del registro e' l'ultima riga di winq_init, quindi non si
 * sa nemmeno se il battito parta una volta. Queste righe lo dicono.
 *
 * SOLO I PRIMI QUATTRO GIRI. A duecento battiti al secondo un info_report per
 * giro renderebbe il registro illeggibile e falserebbe la cosa misurata --
 * misurato oggi: una error_report su pipe lenta costa 356 us, piu' di un
 * intero giro. E' lo stesso motivo per cui le altre diagnostiche riepilogano
 * al secondo invece di stampare per evento. */
static void winq_traccia_avvio(const char *dove)
{
    static int acceso = -1;
    static int giri;

    if (acceso < 0) {
        const char *v = getenv("WINQ_TRACCIA_AVVIO");

        acceso = (v && *v == '1') ? 1 : 0;
    }
    if (!acceso || giri >= 16) {
        return;
    }
    giri++;
    info_report("winq-traccia: %s", dove);
}

static void winq_tick(void *opaque)
{
    /* Il drenaggio c'e' anche qui, oltre alla bottom half: e' la rete di
     * sicurezza se una sveglia si perdesse, e costa un confronto quando la coda
     * e' vuota.
     *
     * Il tubo del guscio invece PUO' stare solo qui. Prima viveva dentro la
     * pompa dei messaggi, ma non per una ragione sua: stava li' perche' la pompa
     * era l'unica cosa che girasse spesso. Non ha mai avuto bisogno della
     * finestra, e dal thread della finestra non potrebbe girare -- il comando
     * che porta finisce in dpy_set_ui_info, che vuole il BQL. */
    winq_traccia_avvio("battito: entrato");
    winq_coda_drena();
    winq_traccia_avvio("battito: coda drenata");
    winq_tubo_passo();
    winq_traccia_avvio("battito: tubo passato");
    winq_presenta_a_cadenza();
    winq_traccia_avvio("battito: presentato");
    timer_mod(winq_battito,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + winq_periodo_ms);
}
/* DIRE AL GUEST A CHE FREQUENZA VA IL PANNELLO DELL'HOST.
 *
 * IL DIFETTO, riferito dall'utente come "il refresh rate mi sembra un po' cosi' cosi',
 * non proprio fluidissimo, comunque perfettamente usabile". Non era un problema di
 * velocita': i tempi di disegno misurati sono 5 ms di mediana con 0,69% di frame
 * scattosi, cioe' ottimi. Era la CADENZA.
 *
 * MISURATO:
 *     pannello dell'host    60 Hz
 *     display del guest     74,99 Hz
 *
 * Il guest produce 75 frame al secondo su un pannello che ne mostra 60: quindici al
 * secondo vengono scartati, e in modo irregolare -- quattro mostrati, uno perso, e
 * ancora. Uno scatto visibile ogni ~80 ms, che nessuna ottimizzazione del disegno
 * tocca, perche' i frame vengono disegnati bene e poi buttati.
 *
 * DA DOVE VIENE IL 75. Da QEMU, hw/display/edid-generate.c:390:
 *     uint32_t refresh_rate = info->refresh_rate ? info->refresh_rate : 75000;
 * cioe' 75 Hz quando nessuno dice altro. Quel campo arriva dal backend di display
 * attraverso dpy_set_ui_info -> virtio_gpu_base_ui_info
 * (hw/display/virtio-gpu-base.c:110), e nessun backend di QEMU lo imposta: ui/sdl2.c
 * chiama dpy_set_ui_info al ridimensionamento passando solo larghezza e altezza, con
 * refresh_rate lasciato a zero. Da qui il fatto che anche -display sdl misuri 74,99 Hz.
 *
 * Con 60 contro 60 il rapporto e' 1:1 esatto: ogni frame del guest corrisponde a una
 * scansione del pannello. Su un pannello a 120 Hz il guest andra' a 120, perche' il
 * valore si LEGGE invece di scriverlo come costante -- una costante 60 sarebbe giusta
 * su questa macchina e sbagliata sulla prossima.
 *
 * SI PASSA LA RISOLUZIONE ATTUALE E NON QUELLA DELLA FINESTRA, deliberatamente.
 * QemuUIInfo porta insieme geometria e frequenza, e passare la dimensione della
 * finestra farebbe RIDIMENSIONARE il guest -- che e' cio' che fa sdl, ed e' un
 * cambiamento di comportamento a se' (via le bande nere, ma anche relayout di Android e
 * mappatura del tocco da rifare). Qui si cambia una cosa sola.
 *
 * SI RIMANDA A OGNI CAMBIO DI RISOLUZIONE, e non una volta sola. Il primo tentativo
 * agganciava il primo frame con una risoluzione nota, e quella era 640x480: il
 * framebuffer di avvio, prima che Android imposti il modo vero. Il risultato e' che al
 * guest veniva detto "sei 640x480 a 60 Hz", la frequenza usciva giusta e la
 * RISOLUZIONE crollava -- misurato con "wm size" -> "Physical size: 640x480".
 *
 * Il rimedio non e' indovinare il momento giusto, che e' la stessa scommessa con un
 * numero diverso: si ricorda cosa si e' detto e si corregge appena diventa falso. Non
 * c'e' anello di ritorno, perche' si passa la risoluzione ATTUALE del guest: e'
 * un'istruzione che non chiede di cambiare nulla, e porta con se' la frequenza. */
/* UNICA DECISIONE su quale frequenza annunciare al guest: WINQ_HZ se presente,
 * altrimenti quella dichiarata dal pannello via GetDeviceCaps(VREFRESH).
 *
 * DUE CHIAMANTI, ed e' la ragione per cui questa funzione esiste: winq_annuncia_
 * frequenza (al cambio di scanout) e winq_imposta_risoluzione (alla rotazione).
 * Prima ciascuno decideva per conto proprio, e winq_imposta_risoluzione rileggeva
 * sempre GetDeviceCaps ignorando WINQ_HZ. Con hz=120 come default nel guscio il
 * sintomo era silenzioso: si avviava a 120, si premeva "ruota", e la frequenza
 * tornava a quella del pannello senza che una sola riga di registro lo dicesse --
 * la fluidita' cambiava dopo la rotazione e non tornava, e il sintomo non nominava
 * la causa. E' la stessa classe di correzione gia' fatta in questo file per il
 * calcolo del bordo, vedi winq_dimensiona_cliente qui sopra.
 *
 * WINQ_HZ forza il valore, e serve a MISURARE invece di scegliere a intuito.
 *
 * Perche' esiste: questo pannello ha refresh variabile. Dichiara 24, 30, 48, 60,
 * 75, 100 e 120 Hz, con MaxRefreshRate 120, e Windows lo commuta da se' col
 * Dynamic Refresh Rate. GetDeviceCaps(VREFRESH) restituisce quindi un'ISTANTANEA
 * di un valore che cambia, e sceglierne uno "giusto" e' una decisione, non una
 * lettura.
 *
 * Il 60 che si ottiene di fatto e' difendibile per una ragione precisa: divide
 * esattamente sia 60 sia 120, quindi resta senza scatti in ENTRAMBI gli stati del
 * DRR -- a 120 ogni frame del guest dura due scansioni, a 60 una. Chiedere 120
 * darebbe piu' fluidita' quando il pannello e' a 120 e lavoro buttato quando
 * scende a 60. Quale dei due convenga e' una misura, non un'opinione, e questa
 * variabile e' il modo di farla:
 *     WINQ_HZ=120 -> una misura di fluidita' sul guest
 *
 * Ritorna -1 se GetDC sulla finestra fallisce (causa: la finestra, non il
 * pannello), 0 o 1 se GetDeviceCaps(VREFRESH) risponde cosi' (causa: il
 * pannello non dichiara una frequenza utile). Sono due guasti diversi con due
 * cause diverse, e il chiamante (winq_annuncia_frequenza) li distingue: prima
 * GetDC fallita ritornava 0 e finiva confusa con un VREFRESH davvero 0,
 * incolpando il pannello di un problema che non era suo. */
static int winq_hz_da_annunciare(void)
{
    const char *forzato = getenv("WINQ_HZ");
    HDC dc;
    int hz;

    if (forzato && *forzato) {
        return atoi(forzato);
    }
    dc = GetDC(winq.hwnd);
    if (!dc) {
        return -1;
    }
    hz = GetDeviceCaps(dc, VREFRESH);
    ReleaseDC(winq.hwnd, dc);
    return hz;
}

static void winq_annuncia_frequenza(void)
{
    static uint32_t detto_w, detto_h, detto_hz;
    QemuUIInfo info;
    int hz;

    /* SOLO DAL PERCORSO DI SCANOUT. Il primo tentativo annunciava alla prima
     * risoluzione nota qualunque, e quella era il framebuffer di avvio: al guest
     * veniva detto "sei 640x480 a 60 Hz", la frequenza usciva giusta e la risoluzione
     * crollava, in modo AUTO-CONSISTENTE -- una volta scesi a 640x480 nulla cambiava
     * piu', quindi nemmeno il rimando a ogni cambio poteva accorgersene. */
    if (!winq_present_in_scanout() ||
        winq.geom.guest_w <= 0 || winq.geom.guest_h <= 0) {
        return;
    }
    if (detto_w == (uint32_t)winq.geom.guest_w &&
        detto_h == (uint32_t)winq.geom.guest_h && detto_hz) {
        return;   /* gia' detto, e ancora vero */
    }

    hz = winq_hz_da_annunciare();

    /* Stesso hz, riusato per l'intervallo minimo di presentazione: vedi
     * winq_presenta_ricalcola_intervallo per il perche' non si chiama
     * winq_hz_da_annunciare() una seconda volta. */
    winq_presenta_ricalcola_intervallo(hz);

    /* -1 e' GetDC fallita sulla finestra: colpa della finestra, non del
     * pannello, e probabilmente transitoria (un HWND valido quasi non fa
     * fallire GetDC). Non si fissa il memo: al prossimo scanout si riprova,
     * invece di restare bloccati sul default di QEMU per il resto della
     * sessione come faceva il ramo qui sotto prima di questa correzione. */
    if (hz == -1) {
        info_report("winq: GetDC on the window failed, cannot "
                    "read the panel refresh rate: keeping the default of "
                    "QEMU at 75 Hz, it will be retried at the next scanout");
        return;
    }

    /* GetDeviceCaps restituisce 0 o 1 per "hardware default", e sono valori
     * da non girare al guest: un EDID a 1 Hz e' peggio di uno a 75. In quel caso si
     * lascia decidere il default di QEMU. */
    if (hz <= 1) {
        info_report("winq: the panel declares no refresh rate (VREFRESH=%d), "
                    "keeping QEMU's default of 75 Hz", hz);
        detto_w = winq.geom.guest_w;
        detto_h = winq.geom.guest_h;
        detto_hz = 1;   /* non zero, per non riprovare a ogni frame */
        return;
    }

    memset(&info, 0, sizeof(info));
    info.width = winq.geom.guest_w;
    info.height = winq.geom.guest_h;
    info.refresh_rate = (uint32_t)hz * 1000;   /* QEMU la vuole in milliHz */

    /* delay = false: non e' la raffica di un ridimensionamento, e' un annuncio unico. */
    if (dpy_set_ui_info(winq.dcl.con, &info, false) == 0) {
        info_report("winq: announced the panel refresh rate to the guest, %d Hz "
                    "(%dx%d). Without it QEMU would declare 75, and on a panel at "
                    "%d Hz the extra frames would be dropped in jerks.",
                    hz, info.width, info.height, hz);
    } else {
        info_report("winq: the device does not accept QemuUIInfo, the rate stays "
                    "QEMU's own (75 Hz)");
    }
    detto_w = info.width;
    detto_h = info.height;
    detto_hz = info.refresh_rate;
}

/* Cambia la risoluzione del guest e adegua la finestra.
 *
 * Riusa lo stesso dpy_set_ui_info con cui si annuncia la frequenza: QemuUIInfo
 * porta geometria E frequenza insieme, quindi la frequenza va ripassata o si
 * tornerebbe ai 75 Hz di default di QEMU (hw/display/edid-generate.c:390) e si
 * riavrebbero gli scatti di 75 su 60. */
void winq_imposta_risoluzione(int w, int h)
{
    QemuUIInfo info;
    /* Stessa decisione di winq_annuncia_frequenza, tramite winq_hz_da_annunciare:
     * prima questa funzione rileggeva GetDeviceCaps per conto proprio e ignorava
     * WINQ_HZ, quindi una rotazione riportava in silenzio il guest alla frequenza
     * del pannello anche con hz forzato all'avvio. Vedi il commento sopra
     * winq_hz_da_annunciare per il sintomo misurato e la ragione del pannello. */
    int hz = winq_hz_da_annunciare();

    /* Stesso hz, riusato per l'intervallo minimo di presentazione: vedi
     * winq_presenta_ricalcola_intervallo per il perche' non si chiama
     * winq_hz_da_annunciare() una seconda volta. */
    winq_presenta_ricalcola_intervallo(hz);

    memset(&info, 0, sizeof(info));
    info.width = (uint32_t)w;
    info.height = (uint32_t)h;
    /* hz<=1 (sconosciuto, o pannello che non dichiara una frequenza: vedi
     * winq_hz_da_annunciare e il confronto gemello in winq_annuncia_frequenza)
     * non va girato al guest: si passa 0, che QemuUIInfo legge come "non
     * specificato" e lascia decidere il default di QEMU. */
    info.refresh_rate = (hz > 1) ? (uint32_t)hz * 1000 : 0;

    if (dpy_set_ui_info(winq.dcl.con, &info, false) != 0) {
        info_report("winq: the device does not accept QemuUIInfo, resolution "
                    "invariata");
        return;
    }
    info_report("winq: guest resolution -> %dx%d at %d Hz", w, h, hz);

    /* La finestra segue: una risoluzione verticale in una finestra orizzontale
     * darebbe bande nere enormi, che e' quasi peggio di non ruotare. Si tiene la
     * posizione e si cambiano solo le dimensioni, tramite winq_dimensiona_cliente
     * (sopra winq_create_window).
     *
     * PERCHE' QUELL'AIUTO E NON AdjustWindowRect QUI, ed e' la prima delle due
     * trappole della consapevolezza del DPI. AdjustWindowRect calcola lo
     * spessore del bordo col DPI di SISTEMA rilevato all'AVVIO DEL PROCESSO:
     * dentro un processo per-monitor-v2 quel numero e' fermo, mentre il bordo
     * vero segue il monitor su cui la finestra sta. winq_dimensiona_cliente
     * chiede invece il DPI alla finestra stessa (winq_fn_dpi), che e' quello
     * vero del monitor su cui sta davvero.
     *
     * SINTOMO SE SI USASSE AdjustWindowRect: dopo una rotazione l'area cliente
     * NON corrisponderebbe alla risoluzione chiesta al guest, e ricomparirebbero
     * bande nere o un ritaglio. Si vedrebbe SOLO dopo aver premuto "ruota", non
     * all'avvio -- cioe' nel modo piu' difficile da attribuire a questa
     * modifica. */
    /* SI POSTA, NON SI MANDA. SetWindowPos consegna WM_SIZE in modo SINCRONO al
     * thread proprietario della finestra: chiamarla da qui bloccherebbe il ciclo
     * principale ogni volta che l'utente sta tenendo il bordo, cioe' rifarebbe
     * il guasto da 23.848 ms su un percorso -- la rotazione -- che nessuno
     * sospetterebbe. Vedi il commento su WINQ_MSG_DIMENSIONA in winq.h. */
    PostMessageA(winq.hwnd, WINQ_MSG_DIMENSIONA, (WPARAM)w, (LPARAM)h);
}

static void winq_display_refresh(DisplayChangeListener *dcl)
{
    /* LA POMPA VA PER PRIMA, ED E' IL TOCCO A PRETENDERLO. MISURATO.
     *
     * Svuotare la coda Win32 all'inizio significa che i WM_POINTER* di questo
     * giro vengono tradotti e messi in coda a virtio PRIMA che
     * graphic_hw_update chieda al guest di produrre il frame: il guest vede
     * l'input e disegna la sua reazione nello stesso giro.
     *
     * Con la pompa per ULTIMA -- l'ordine di sdl2_gl_refresh, che ho ripristinato
     * per errore -- al guest non arriva NULLA: registrazione di due minuti con
     * getevent, zero BTN_TOUCH e zero posizioni, contro 37 pressioni e 37
     * rilasci misurati con l'ordine corretto. Non e' un ritardo di un giro: e'
     * il tocco che non passa.
     *
     * Non introduce ricorsione: winq_pump_messages non chiama ne'
     * graphic_hw_update ne' winq_present_frame -- WM_SIZE e WM_PAINT alzano solo
     * un flag, che winq_present_frame consuma piu' sotto.
     *
     * Il prezzo, misurato e accettato: un flag alzato adesso viene visto al giro
     * successivo, cioe' ~30 ms di ritardo su un ridimensionamento. Invisibile, e
     * comunque preferibile a un tocco che non arriva.
     */
    /* CHI SI MANGIA IL CICLO PRINCIPALE, dietro WINQ_SND_DIAG.
     *
     * Il timer audio di QEMU e questa funzione girano sullo STESSO thread. Con
     * Spotify che lavora il timer scende da 285 a 89-119 giri al secondo, e
     * l'audio gracchia perche' il buffer dell'host resta a secco. La domanda e'
     * quanto di quel tempo se lo prende il disegno: qui si misurano le tre
     * parti separate, cosi' si sa se strozzare la presentazione servirebbe
     * oppure se il costo sta nel guest che produce il frame (graphic_hw_update)
     * e allora la presentazione non c'entra.
     */
    if (getenv("WINQ_SND_DIAG")) {
        static int64_t winq_inizio;
        static unsigned long winq_giri, winq_drena, winq_disegno, winq_presenta;
        int64_t t0 = g_get_monotonic_time();
        int64_t t1, t2, t3;

        winq_coda_drena();
        t1 = g_get_monotonic_time();
        winq_annuncia_frequenza();
        graphic_hw_update(dcl->con);
        t2 = g_get_monotonic_time();
        winq_presenta_a_cadenza();
        t3 = g_get_monotonic_time();

        winq_giri++;
        winq_drena += (unsigned long)(t1 - t0);
        winq_disegno += (unsigned long)(t2 - t1);
        winq_presenta += (unsigned long)(t3 - t2);
        if (!winq_inizio) {
            winq_inizio = t0;
        } else if (t0 - winq_inizio >= 1000000) {
            error_report("winq-schermo/s: giri=%lu drenaggio=%lu us disegno=%lu us "
                         "presenta=%lu us (totale %lu ms al secondo)",
                         winq_giri, winq_drena / winq_giri,
                         winq_disegno / winq_giri, winq_presenta / winq_giri,
                         (winq_drena + winq_disegno + winq_presenta) / 1000);
            winq_inizio = t0;
            winq_giri = winq_drena = winq_disegno = winq_presenta = 0;
        }
        return;
    }

    winq_coda_drena();
    winq_annuncia_frequenza();
    graphic_hw_update(dcl->con);
    winq_presenta_a_cadenza();
}

/* Un solo insieme di ops, con i callback GL sempre presenti: winq non ha un
 * percorso 2D alternativo come ui/sdl2.c (dcl_2d_ops), perche' senza GL non ha
 * proprio modo di disegnare -- e' EGL che porta i pixel sulla finestra. Per
 * questo winq_early_init rifiuta gl=off invece di registrare ops mutilate.
 * I nomi dei campi e le firme vengono dai sorgenti di QEMU. */
const DisplayChangeListenerOps winq_dcl_ops = {
    .dpy_name             = "winq",
    .dpy_refresh          = winq_display_refresh,
#ifdef CONFIG_OPENGL
    .dpy_gfx_switch       = winq_present_gfx_switch,
    .dpy_gfx_update       = winq_present_gfx_update,
    .dpy_gfx_check_format = console_gl_check_format,

    .dpy_gl_scanout_texture = winq_present_scanout_texture,
    .dpy_gl_scanout_disable = winq_present_scanout_disable,
    .dpy_gl_update          = winq_present_gl_update,
#endif
};

/* Perche' serve un .early_init, misurato non previsto: "-device
 * virtio-gpu-gl-pci" (che la riga di comando usa a prescindere dal backend
 * -display, per restare confrontabile con sdl) si rifiuta di fare realize se il
 * flag globale display_opengl non e' alzato, e QEMU esce con "The display
 * backend does not have OpenGL support enabled" -- prima ancora di arrivare a
 * winq_init. Il flag lo legge hw/display/virtio-gpu-gl.c:127, e non gli importa
 * quale backend lo abbia alzato. Punto di aggancio e pattern copiati da
 * ui/sdl2.c:845-853 (sdl2_display_early_init).
 *
 * Qui NON si alza il flag a mano: lo fa egl_init dentro
 * winq_present_early_init (ui/egl-helpers.c:733), dopo aver davvero
 * inizializzato EGL. La differenza non e' di stile: alzare il flag senza avere
 * un contesto GL vero e' esattamente cio' che una misura ha misurato, e porta a
 * "assertion failed: (con->gl)" in ui/console.c:985 a poche righe di avvio del
 * guest, perche' virglrenderer chiede subito un contesto con dpy_gl_ctx_create
 * (hw/display/virtio-gpu-virgl.c:1315-1327). Il flag e le quattro
 * DisplayGLCtxOps vanno insieme o non vanno.
 *
 * L'inizializzazione di EGL sta qui e non in winq_init perche' virglrenderer
 * guarda qemu_egl_display quando sceglie con quali callback partire
 * (hw/display/virtio-gpu-virgl.c:1445-1458), e .early_init e' il solo punto
 * garantito prima della realize dei device. */
static void winq_early_init(DisplayOptions *o)
{
    DisplayGLMode mode = DISPLAY_GL_MODE_ON;

    /* PRIMA DI QUALUNQUE ALTRA COSA, e non "presto": la consapevolezza del DPI
     * non ha effetto retroattivo su una finestra gia' creata, e .early_init e' il
     * primo punto garantito prima della realize dei device -- quindi molto prima
     * di winq_create_window, che gira dentro winq_init. */
    winq_dpi_dichiara();

    if (o->has_gl) {
        mode = o->gl;
    }

    if (mode == DISPLAY_GL_MODE_OFF) {
        /* Si dice la causa e si esce, invece di aprire una finestra nera muta:
         * questa versione di winq presenta solo tramite EGL, quindi senza GL
         * non ha nessun percorso di disegno -- non una versione degradata. */
        error_report("winq: needs \"-display winq,gl=on\": without GL this "
                     "backend has no way to draw on the window. "
                     "Alternatively use -display sdl.");
        exit(1);
    }

#ifdef CONFIG_OPENGL
    if (!winq_present_early_init(mode)) {
        exit(1);   /* la causa e' gia' stata riportata, con il rimedio */
    }
#else
    error_report("winq: this QEMU was built without OpenGL. "
                 "Use -display sdl.");
    exit(1);
#endif
}

static void winq_init(DisplayState *ds, DisplayOptions *o)
{
    QemuConsole *con = NULL;
    int i;

    /* Il console grafico, non il primo console qualunque: con -serial e
     * -monitor in gioco l'indice 0 non e' garantito essere quello del
     * virtio-gpu. Stessa scansione di ui/egl-headless.c:223-229. */
    for (i = 0;; i++) {
        QemuConsole *c = qemu_console_lookup_by_index(i);
        if (!c) {
            break;
        }
        if (qemu_console_is_graphic(c)) {
            con = c;
            break;
        }
    }
    if (!con) {
        error_report("winq: no graphics console to show. "
                     "Is a display device missing from the command line?");
        exit(1);
    }

    /* Le dimensioni iniziali sono provvisorie: il primo dpy_gfx_switch o
     * dpy_gl_scanout_texture del guest le correggera'. 1280x800 e' quanto
     * virtio-gpu annuncia oggi con questa riga di comando. */
    if (!winq_avvia_thread(1280, 800)) {
        error_report("winq: cannot create the window. Fall back to -display sdl.");
        exit(1);
    }

    /* La bottom half subito dopo: da qui in poi il thread della finestra puo'
     * svegliare il ciclo principale invece di far aspettare i suoi record fino
     * al battito. Prima di questa riga i record non si perdono comunque -- il
     * primo drenaggio li trova in coda. */
    winq_bh = qemu_bh_new(winq_bh_corpo, NULL);

    /* Prima di qualunque messaggio: la pompa gira solo dentro
     * winq_display_refresh, che parte con register_displaychangelistener piu'
     * in basso, ma la finestra esiste gia' e un dito appoggiato adesso
     * accoderebbe WM_POINTERDOWN. Per struct touch_slot lo zero non e' lo stato
     * di riposo, vedi winq_input_init. */
    winq_input_init();

    winq.con = con;
    winq.dcl.con = con;    /* senza questo dpy_gl_scanout_texture non ci
                            * arriverebbe mai: ui/console.c:1037 consegna solo
                            * ai listener con dcl->con == con */
    winq.dcl.ops = &winq_dcl_ops;

#ifdef CONFIG_OPENGL
    /* Ordine obbligato: prima le operazioni di contesto sul console, poi la
     * superficie di presentazione, poi la registrazione del listener.
     * qemu_console_set_display_gl_ctx deve precedere
     * register_displaychangelistener perche' quest'ultima chiama
     * console_compatible_with (ui/console.c:570-598), che con un device
     * GRAPHIC_FLAGS_GL pretende che il console abbia gia' un contesto e
     * altrimenti fallisce con &error_fatal. */
    winq.dgc.ops = &winq_gl_ctx_ops;
    qemu_console_set_display_gl_ctx(con, &winq.dgc);

    if (!winq_present_init()) {
        exit(1);   /* causa e rimedio gia' riportati da winq_present_init */
    }
#endif

    register_displaychangelistener(&winq.dcl);

    /* Il tubo dei comandi dal guscio: una rotazione arriva da li'. Aperto qui e
     * non prima perche' non ha senso ricevere comandi finche' il console non
     * esiste ancora. */
    winq_tubo_apri();

    /* Il battito rapido dell'input, armato per ultimo: prima di questa riga non
     * esistono ne' la console ne' il contesto, e presentare non avrebbe senso. */
    winq_periodo_ms = winq_leggi_periodo();
    winq_battito = timer_new_ms(QEMU_CLOCK_REALTIME, winq_tick, NULL);
    timer_mod(winq_battito, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    info_report("winq: input heartbeat every %d ms "
                "(WINQ_PERIODO_MS to change it)", winq_periodo_ms);

    /* Letta una volta sola, come winq_periodo_ms qui sopra: vedi il commento
     * su winq_presenta_leggi_forzato_ms per il perche'. Se non forzata,
     * winq_presenta_intervallo_ms resta al ripiego di 16 ms finche' il primo
     * scanout non fa conoscere l'hz vero tramite
     * winq_presenta_ricalcola_intervallo. */
    winq_presenta_ms_forzato = winq_presenta_leggi_forzato_ms();
    if (winq_presenta_ms_forzato) {
        winq_presenta_intervallo_ms = winq_presenta_ms_forzato;
        info_report("winq: minimum interval between two presentations forced to "
                    "%d ms (WINQ_PRESENTA_MS)", winq_presenta_intervallo_ms);
    } else {
        info_report("winq: minimum interval between two presentations %d ms "
                    "(fallback, hz not announced yet)",
                    winq_presenta_intervallo_ms);
    }
}

static QemuDisplay qemu_display_winq = {
    .type       = DISPLAY_TYPE_WINQ,
    .early_init = winq_early_init,
    .init       = winq_init,
};

static void register_winq(void)
{
    qemu_display_register(&qemu_display_winq);
}

type_init(register_winq);
