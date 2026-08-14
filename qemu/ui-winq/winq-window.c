/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-window.c -- il lato THREAD DELLA FINESTRA del backend -display winq.
 *
 * Qui c'e' la finestra, il suo thread, il wndproc e la traduzione dei messaggi
 * in record. Il disegno e il contesto GL stanno in winq-present.c; tutto cio'
 * che parla con QEMU sta in winq-ciclo.c.
 *
 * LA REGOLA DI QUESTO FILE, E NON E' UNO STILE: IL WNDPROC NON CHIAMA NIENTE DI
 * QEMU. Non prende il BQL, non tocca winq.geom, non chiama
 * winq_handle_pointer_event. Traduce ogni messaggio in un record, lo accoda
 * (winq-coda.h) e sveglia il ciclo principale, che drena e chiama.
 *
 * PERCHE'. Fino al la pompa dei messaggi viveva dentro winq_tick,
 * cioe' un timer del ciclo principale di QEMU. Una finestra Win32 appartiene al
 * thread che l'ha CREATA, e li' gira anche il ciclo modale in cui Windows entra
 * dentro DefWindowProc quando l'utente afferra un bordo o la barra del titolo:
 * finche' non si rilascia, il ciclo principale non gira, il BQL non viene
 * rilasciato e la macchina virtuale e' FERMA. MISURATO: un buco di 23.848 ms
 * tenendo il bordo e uno di 11.029 ms spostando la finestra, contro zero buchi a
 * riposo su 250 campioni.
 *
 * Creare la finestra su un thread suo e' tutto cio' che serve perche' quel ciclo
 * modale non possa piu' toccare il ciclo principale. Ma vale solo finche' la
 * regola qui sopra regge: un wndproc che prendesse il BQL e poi entrasse in
 * DefWindowProc rimetterebbe il guasto esattamente dov'era, e in silenzio.
 *
 * IL CONTESTO GL NON STA QUI. Resta sul ciclo principale, dov'era: e' la ragione
 * per cui questo scorporo si e' potuto fare senza riaprire la condivisione del
 * contesto fra thread, che la spec di host-window vietava. MISURATO prima di
 * scrivere una riga (qemu/probe/sonda-thread.c): SwapBuffers chiamata dal
 * thread del contesto NON aspetta il thread proprietario della finestra mentre
 * quello e' dentro un ciclo modale -- peggiore 11,6 ms su 1772 campioni.
 *
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/thread.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-control.h"
#include "qemu/error-report.h"
#include "system/system.h"
#include "ui/console.h"
#include "winq.h"
#include "winq-coda.h"
#include "winq-mappa.h"

WinqState winq;

/* Definita in fondo, insieme alla creazione della finestra, e usata dal wndproc
 * che sta prima: e' l'unica ragione di questa dichiarazione anticipata. Resta
 * static perche' e' chiamata SOLO da questo file -- il ciclo principale non la
 * chiama piu' da se', posta WINQ_MSG_DIMENSIONA e la lascia eseguire al thread
 * proprietario della finestra. */
static void winq_dimensiona_cliente(int w, int h);

/* LA CONSAPEVOLEZZA DEL DPI, e perche' le tre funzioni si risolvono a mano.
 *
 * PERCHE' SERVE. Lo schermo di questa macchina e' 2880x1920. Un processo che non
 * dichiara di conoscere il DPI ne vede 1440x960 -- MISURATO: GetSystemMetrics
 * restituisce 1440x960 a un processo non-aware e 2880x1920 a uno
 * per-monitor-v2, sulla stessa macchina nello stesso istante. Windows mente al
 * processo, il processo disegna in quello spazio, e poi il sistema ingrandisce
 * di 2x: sono DUE scalature in fila, e la seconda e' quella che sfoca.
 * Dichiarando la consapevolezza la finestra riceve i pixel veri e la scalatura
 * resta una sola, fatta dalla GPU con filtro lineare sul contenuto del guest.
 *
 * PERCHE' I PROTOTIPI SE LI DICHIARA QUESTO FILE. qemu/osdep.h:81-82 fissa
 * _WIN32_WINNT a 0x0602 (Windows 8) per TUTTO l'albero, e i mingw-w64 headers
 * nascondono dietro NTDDI_VERSION >= WIN10_RS1 i prototipi di
 * SetProcessDpiAwarenessContext, GetDpiForWindow e AdjustWindowRectExForDpi.
 * VERIFICATO compilando, non dedotto: con -D_WIN32_WINNT=0x0602 le tre chiamate
 * danno "call to undeclared function". Alzare _WIN32_WINNT per tutto QEMU
 * sarebbe un innesto molto piu' invasivo di una copia di file (vedi
 * qemu/scripts/innesta-winq.sh) e cambierebbe il comportamento di codice che
 * non e' nostro. Il TIPO DPI_AWARENESS_CONTEXT e le sue costanti NON sono sotto
 * quella guardia (windef.h le definisce dentro WINAPI_PARTITION_DESKTOP) e
 * WM_DPICHANGED nemmeno (winuser.h, WINVER >= 0x0601): quelli si riusano.
 *
 * PERCHE' GetProcAddress E NON COLLEGAMENTO DIRETTO, anche dove il prototipo
 * ci fosse: un import di user32.dll non risolvibile fa rifiutare dal caricatore
 * il processo INTERO, prima che esegua una riga. Il ripiego non sarebbe
 * "sfocato" ma "does not start". Le wgl* si caricano a runtime per la stessa
 * ragione.
 *
 * PERCHE' NON UN MANIFEST, che sarebbe la via piu' robusta perche' si applica
 * prima che qualunque codice giri: richiederebbe di toccare la configurazione di
 * compilazione di QEMU, e l'innesto e' oggi una copia di file piu' una patch di
 * due punti. Un innesto che tocca meno cose si riapplica su una versione nuova
 * di QEMU senza sorprese. */
/* BOOL, non DPI_AWARENESS_CONTEXT: e' SetTHREADDpiAwarenessContext a
 * restituire il contesto precedente. Con il tipo sbagliato l'esito si
 * testerebbe come puntatore, e su AArch64 i 32 bit alti di x0 dopo un
 * ritorno BOOL non sono specificati: un FALSE con spazzatura in alto
 * prenderebbe il ramo del successo e il log direbbe il contrario del vero. */
typedef BOOL (WINAPI *WinqFnContesto)(DPI_AWARENESS_CONTEXT);
typedef UINT (WINAPI *WinqFnDpiFinestra)(HWND);
typedef BOOL (WINAPI *WinqFnAdjustPerDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);

static WinqFnDpiFinestra winq_fn_dpi;
static WinqFnAdjustPerDpi winq_fn_adjust;

/* Va chiamata PRIMA che esista qualunque finestra: la dichiarazione non ha
 * effetto retroattivo su una finestra gia' creata. .early_init gira prima di
 * winq_create_window, quindi e' il posto. */
void winq_dpi_dichiara(void)
{
    HMODULE u = GetModuleHandleA("user32.dll");
    WinqFnContesto imposta;

    if (!u) {
        info_report("winq: user32.dll not reachable (%lu): continuing without "
                    "DPI awareness, the window will stay blurry on a "
                    "scaled screen", GetLastError());
        return;
    }

    /* Le due letture si risolvono comunque: senza consapevolezza
     * GetDpiForWindow risponde 96, che e' il valore giusto per un processo a cui
     * Windows sta mentendo, e chi le usa non ha un secondo caso da trattare. */
    winq_fn_dpi = (WinqFnDpiFinestra)(void *)
        GetProcAddress(u, "GetDpiForWindow");
    winq_fn_adjust = (WinqFnAdjustPerDpi)(void *)
        GetProcAddress(u, "AdjustWindowRectExForDpi");

    imposta = (WinqFnContesto)(void *)
        GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (!imposta) {
        info_report("winq: SetProcessDpiAwarenessContext is not there (needs "
                    "Windows 10 1703): continuing without DPI "
                    "DPI, the window stays usable and blurry as before");
        return;
    }
    if (imposta(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        info_report("winq: declared per-monitor-v2, the window will get "
                    "physical screen pixels and there is a single scaling step");
        return;
    }
    /* Il ripiego copre il caso in cui la funzione ci sia e rifiuti QUEL
     * contesto: la funzione e il contesto v2 sono arrivati insieme in Windows 10
     * 1703, quindi questo ramo non e' una compatibilita' con un Windows piu'
     * vecchio e non si finge tale. Costa tre righe e lascia la finestra nitida. */
    if (imposta(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)) {
        info_report("winq: per-monitor-v2 refused, declared system-aware "
                    "as a fallback: the pixels are real, but moving to a "
                    "screen with a different scale will not be followed");
        return;
    }
    {
        DWORD err = GetLastError();

        /* ERROR_ACCESS_DENIED (5): SetProcessDpiAwarenessContext fallisce cosi'
         * quando la consapevolezza del processo e' GIA' stata impostata da
         * qualcun altro. In quel caso il processo E' consapevole, e questo
         * messaggio non va letto come "sfocata": lo sarebbe solo se l'errore
         * fosse un altro. */
        if (err == ERROR_ACCESS_DENIED) {
            info_report("winq: no DPI awareness accepted (%lu): "
                        "ERROR_ACCESS_DENIED means it was already set "
                        "by someone else, so the process IS aware, "
                        "not blurry", err);
        } else {
            info_report("winq: no DPI awareness accepted (%lu): "
                        "the window stays usable and blurry as before", err);
        }
    }
}

/* IL SEGNALE AL GUSCIO, e la scadenza che lo rende sicuro.
 *
 * Si APRE l'evento per nome invece di crearlo: crearlo riuscirebbe anche senza
 * guscio, e allora si nasconderebbe la finestra aspettando qualcuno che non
 * esiste -- QEMU vivo e invisibile, chiudibile solo dal Task Manager, cioe'
 * PEGGIO di prima. Aprire fallisce se il guscio non c'e', ed e' esattamente la
 * distinzione che serve. */
static bool winq_segnala_chiusura(void)
{
    HANDLE e = OpenEventA(EVENT_MODIFY_STATE, FALSE,
                          "AndroidRuntimeGuscio.chiusura");

    if (!e) {
        return false;
    }
    SetEvent(e);
    CloseHandle(e);
    info_report("winq: close signalled to the shell, waiting for the shutdown "
                "of Android");
    return true;
}


/* LA POLITICA DI ATTESA A CODA PIENA, e perche' aspettare e' la scelta giusta.
 *
 * Se la coda e' piena, il ciclo principale e' indietro. Le due alternative sono
 * scartare adesso o aspettare che si liberi, e aspettare qui NON ha il costo che
 * avrebbe avuto prima di questo lavoro: ferma il thread della FINESTRA, non il
 * ciclo principale, quindi la macchina virtuale continua a girare. E' esattamente
 * il punto di tutto lo scorporo.
 *
 * Scartare in silenzio sarebbe invece il difetto che questo codice ha gia' pagato
 * piu' volte: un evento buttato senza una riga non si distingue da un evento mai
 * arrivato, e si finisce a cercarne la causa dentro Android.
 *
 * IL LIMITE ESISTE PERCHE' UN CICLO PRINCIPALE MORTO NON DEVE RENDERE LA FINESTRA
 * IMPOSSIBILE DA CHIUDERE. Dopo cento millisecondi si scarta contando: la
 * finestra torna a rispondere degradata invece che muta. Anche i 100 ms sono una
 * scelta e non una misura -- se la coda si riempie davvero, il registro lo dira'
 * e allora ci sara' un numero da guardare. */
#define WINQ_ATTESA_MAX_MS 100

static void winq_accoda(const WinqRec *r)
{
    int atteso;

    for (atteso = 0; atteso < WINQ_ATTESA_MAX_MS; atteso++) {
        if (winq_coda_metti(r)) {
            winq_sveglia();
            return;
        }
        Sleep(1);
    }
    winq_coda_conta_scarto();
    /* warn_report_once e non warn_report: se la coda si riempie, si riempie a
     * raffica, e mille righe uguali nasconderebbero cio' che viene dopo. Il
     * totale resta in winq_coda_scartati(), che il drenaggio riporta quando
     * cambia. */
    warn_report_once("winq: queue full for over %d ms, window event "
                     "dropped. The main loop is behind.",
                     WINQ_ATTESA_MAX_MS);
}

/* I quattro aiuti che costruiscono un record.
 *
 * Esistono perche' il wndproc li chiama da dodici rami diversi, e ripetere il
 * memset dodici volte e' il modo in cui un campo resta sporco in UN ramo solo --
 * cioe' un guasto che si presenta con un gesto e non con gli altri. */
static void winq_accoda_puntatore(uint32_t id, int x, int y, int tocco,
                                  bool pos_valida)
{
    WinqRec r;

    memset(&r, 0, sizeof(r));
    r.tipo = WINQ_REC_PUNTATORE;
    r.id = id;
    r.x = x;
    r.y = y;
    r.tocco = tocco;
    r.pos_valida = pos_valida;
    winq_accoda(&r);
}

static void winq_accoda_tasto(WPARAM vk, LPARAM info, bool premuto)
{
    WinqRec r;

    memset(&r, 0, sizeof(r));
    r.tipo = WINQ_REC_TASTO;
    r.vk = (uint64_t)vk;
    r.info = (int64_t)info;
    r.premuto = premuto;
    winq_accoda(&r);
}

static void winq_accoda_semplice(int tipo)
{
    WinqRec r;

    memset(&r, 0, sizeof(r));
    r.tipo = tipo;
    winq_accoda(&r);
}

/* Una dimensione porta SEMPRE con se' un "esponi": il contenuto non e' cambiato
 * ma il rettangolo in cui va disegnato si', e senza il ridisegno un
 * ridimensionamento a guest fermo lascerebbe l'immagine vecchia con le bande
 * sbagliate finche' il guest non produce un frame nuovo. Stesso ruolo di
 * SDL_WINDOWEVENT_EXPOSED -> sdl2_redraw in ui/sdl2.c. */
static void winq_accoda_dimensione(int w, int h)
{
    WinqRec r;

    memset(&r, 0, sizeof(r));
    r.tipo = WINQ_REC_DIMENSIONE;
    r.w = w;
    r.h = h;
    winq_accoda(&r);
    winq_accoda_semplice(WINQ_REC_ESPONI);
}

/* LA MAPPA DEI TASTI (tastiera -> tocco), vedi winq-mappa.h.
 *
 * La mappa vive SOLO su questo thread: nessun lucchetto, perche' i tasti
 * arrivano tutti qui (WM_KEYDOWN/UP) e nessun altro thread la tocca. I
 * comandi dal ciclo principale arrivano come messaggi POSTATI
 * (WINQ_MSG_MAPPA_*, vedi winq.h), cioe' vengono eseguiti da questo stesso
 * thread quando la pompa dei messaggi li ripesca: non e' una chiamata da un
 * altro thread che attraversa il confine, e' un messaggio che ci arriva
 * come tutti gli altri. */
static Mappa *winq_mappa;
static bool   winq_mappa_accesa;

/* Stato del modo impara. E' SEPARATO dallo stato interno del modulo
 * (m->imp_attivo, privato in winq-mappa.c) perche' questo file deve sapere,
 * PRIMA di chiamare il modulo, quale dei due percorsi seguire su un
 * WM_KEYDOWN o un WM_LBUTTONDOWN: se lo chiedesse al modulo dovrebbe
 * esistere una funzione che esponga imp_attivo, e non ce n'e' una -- il
 * modulo la tiene privata apposta, perche' fuori da mappa_impara_tasto e
 * mappa_impara_punto quello stato non deve influenzare nessun'altra
 * decisione presa altrove. winq_mappa_impara_percorso e' la seconda meta'
 * di un passaggio di proprieta': arriva con WINQ_MSG_MAPPA_IMPARA (g_strdup
 * da winq-ciclo.c), ma il messaggio si consuma subito mentre il modo impara
 * resta aperto magari per piu' di un evento (un tasto, poi un clic; per il
 * joystick due clic) -- quindi la stringa si ricopia qui e si libera solo
 * quando il modo impara si chiude, con mappa_salva o con un annullamento. */
static bool            winq_mappa_impara_attiva;
static MappaImparaTipo winq_mappa_impara_tipo;
static char            *winq_mappa_impara_percorso;

/* Lo stato acceso/spento da riportare quando il modo impara finisce.
 *
 * IL PROPRIETARIO DELLO STATO E' IL GUSCIO (spec sezione 10), e la ragione e'
 * scritta li': se winq decidesse da sola, il bottone e la realta' si
 * disallineerebbero. Prima qui c'era "winq_mappa_accesa = true" alla fine di un
 * apprendimento riuscito: con la mappa SPENTA, dopo l'apprendimento la tastiera
 * risultava tradotta ma il bottone diceva spenta, e Ctrl+Alt+T accendeva invece
 * di spegnere -- andava premuto due volte. Salvare e ripristinare non richiede
 * nessun canale di ritorno verso il guscio: l'ingresso in modo impara e' l'unica
 * cosa che ha spento la mappa, e l'uscita la rimette com'era. */
static bool            winq_mappa_accesa_prima_impara;

/* Converte una posizione in permille dell'area cliente in coordinate cliente.
 *
 * Permille dell'AREA CLIENTE e non della superficie del guest: cosi' il record
 * accodato e' identico a quello di un dito vero appoggiato in quel punto, e il
 * ciclo principale gli applica la stessa conversione. Nessuna doppia
 * conversione, nessun arrotondamento che si accumula.
 *
 * L'ARITMETICA STA NEL MODULO PURO (mappa_pm_a_px / mappa_px_a_pm): li' si puo'
 * provare senza VM che pixel->permille->pixel torni al punto di partenza entro
 * un pixel, come la spec (sezione 13) pretende e come non era vero finche' le
 * due conversioni vivevano qui e troncavano entrambe. Qui resta solo la lettura
 * dell'area cliente, che e' l'unica parte che vuole Windows. */
static void winq_mappa_pm_a_cliente(HWND h, int32_t x_pm, int32_t y_pm,
                                    int *x, int *y)
{
    RECT c;

    GetClientRect(h, &c);
    *x = mappa_pm_a_px(x_pm, c.right - c.left);
    *y = mappa_pm_a_px(y_pm, c.bottom - c.top);
}

/* Il giro inverso di winq_mappa_pm_a_cliente, per il clic del modo impara.
 *
 * La guardia su area cliente vuota (finestra minimizzata, o comunque a zero) e'
 * dentro mappa_px_a_pm: senza, una divisione per zero. Non e' un caso previsto
 * dal modo impara -- che clicca su una finestra visibile -- ma questo file non
 * chiama niente di QEMU e non ha modo di sapere se lo e', quindi si tratta come
 * un dato non disponibile e si risponde zero invece di un comportamento
 * indefinito. */
static void winq_mappa_cliente_a_pm(HWND h, int x, int y,
                                    int *x_pm, int *y_pm)
{
    RECT c;

    GetClientRect(h, &c);
    *x_pm = mappa_px_a_pm(x, c.right - c.left);
    *y_pm = mappa_px_a_pm(y, c.bottom - c.top);
}

/* Dice al modulo quanto e' grande l'area cliente, in pixel.
 *
 * Serve perche' il raggio del joystick e' in permille del LATO CORTO (spec
 * sezione 4) mentre le coordinate sono in permille dei due lati: senza queste
 * due misure lo stick e' un'ELLISSE, e su 1600x900 "destra" sposta il dito di
 * 192 px mentre "su" lo sposta di 108. Vedi mappa_imposta_proporzioni.
 *
 * Va richiamata a OGNI cambio dell'area cliente, non solo al caricamento: la
 * finestra si ridimensiona e la rotazione del guest ne cambia le proporzioni,
 * e proporzioni vecchie riaprirebbero l'ellisse in silenzio. */
static void winq_mappa_aggiorna_proporzioni(HWND h)
{
    RECT c;

    if (!winq_mappa) {
        return;
    }
    GetClientRect(h, &c);
    mappa_imposta_proporzioni(winq_mappa, c.right - c.left, c.bottom - c.top);
}

static void winq_mappa_accoda_azioni(HWND h, const MappaAzione *az, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        int x = 0, y = 0, tocco;

        switch (az[i].tipo) {
        case MAPPA_AZ_BEGIN:  tocco = WINQ_TOCCO_BEGIN;  break;
        case MAPPA_AZ_UPDATE: tocco = WINQ_TOCCO_UPDATE; break;
        default:              tocco = WINQ_TOCCO_END;    break;
        }
        if (az[i].tipo != MAPPA_AZ_END) {
            winq_mappa_pm_a_cliente(h, az[i].x_pm, az[i].y_pm, &x, &y);
        }
        winq_accoda_puntatore(az[i].id, x, y, tocco,
                              az[i].tipo != MAPPA_AZ_END);
    }
}

/* Ritorna true se il tasto e' stato consumato dalla mappa: in quel caso NON va
 * consegnato al guest come tasto. */
static bool winq_mappa_intercetta(HWND h, WPARAM vk, LPARAM info, bool premuto)
{
    MappaAzione az[MAPPA_AZIONI_MAX];
    int n;

    if (!winq_mappa || !winq_mappa_accesa) {
        return false;
    }
    /* Bit 30 di LPARAM: lo stato precedente del tasto. A uno significa che era
     * gia' premuto, cioe' e' ripetizione automatica. */
    n = mappa_tasto(winq_mappa, (int)vk, premuto,
                    premuto && (info & (1 << 30)) != 0,
                    az, MAPPA_AZIONI_MAX);
    if (n == 0) {
        /* Zero puo' voler dire "not mapped" oppure "mappato ma niente da
         * fare", per esempio una ripetizione. Nel secondo caso il tasto e'
         * comunque nostro e non va consegnato al guest, altrimenti tenere
         * premuto W scriverebbe "wwwww" dentro il gioco.
         *
         * "premuto &&" NON e' una guardia di comodo: e' l'unica cosa che evita
         * un tasto INCOLLATO nel guest. Se la mappa si ACCENDE mentre W e' gia'
         * tenuto giu' (fine di un modo impara, scelta di un profilo dal menu),
         * il guest ha gia' ricevuto KEY_W DOWN come tasto normale. Al rilascio
         * mappa_tasto ritorna zero -- per la mappa niente e' cambiato -- e
         * sopprimere quel KEYUP lascerebbe W premuto nel guest PER SEMPRE, con
         * l'autoripetizione di Android sopra e nessuna via d'uscita. La
         * soppressione serve solo a scartare la ripetizione automatica, che e'
         * un WM_KEYDOWN; un UP consegnato senza il suo DOWN e' invece innocuo.
         * Provato nel modulo, prova_rilascio_dopo_accensione_a_tasto_gia_giu. */
        return premuto && mappa_tasto_e_mio(winq_mappa, (int)vk);
    }
    winq_mappa_accoda_azioni(h, az, n);
    return true;
}

/* Azzera solo lo stato LOCALE del modo impara (percorso e flag), senza
 * toccare il modulo. Condivisa da due chiamanti che si trovano in situazioni
 * diverse rispetto al modulo:
 * - winq_mappa_impara_chiudi, qui sotto, che PRIMA annulla nel modulo perche'
 *   il modo impara e' a meta';
 * - il ramo di successo di WM_LBUTTONDOWN, dove mappa_impara_punto ha GIA'
 *   chiuso il modo impara da se' (l'assegnazione e' completa): chiamare
 *   mappa_impara_annulla anche li' annullerebbe un'assegnazione che e' appena
 *   riuscita, invece di limitarsi a liberare la copia locale del percorso. */
static void winq_mappa_impara_azzera(void)
{
    g_free(winq_mappa_impara_percorso);
    winq_mappa_impara_percorso = NULL;
    winq_mappa_impara_attiva = false;
    /* Si RIPRISTINA lo stato che c'era prima, non si accende: lo stato
     * acceso/spento e' del guscio (spec sezione 10), e winq che lo cambia di
     * testa sua e' il modo in cui il bottone comincia a mentire. Qui e non nei
     * chiamanti perche' i modi di uscire dal modo impara sono tre --
     * assegnazione riuscita, assegnazione impossibile, annullamento -- e il
     * ripristino deve essere lo stesso in tutti e tre. */
    winq_mappa_accesa = winq_mappa_accesa_prima_impara;
}

/* Chiude un modo impara aperto, se ce n'era uno: libera il percorso ricevuto
 * con WINQ_MSG_MAPPA_IMPARA e azzera lo stato. Chiamata prima di aprirne un
 * altro (senza, il percorso del precedente perderebbe la memoria) e da
 * WINQ_MSG_MAPPA_CARICA / WINQ_MSG_MAPPA_SPEGNI (senza, un modo impara
 * lasciato a meta' continuerebbe a intercettare tasti e clic dopo che la
 * mappa su cui stava lavorando e' stata sostituita o spenta). */
static void winq_mappa_impara_chiudi(void)
{
    if (!winq_mappa_impara_attiva) {
        return;
    }
    mappa_impara_annulla(winq_mappa);
    winq_mappa_impara_azzera();
}

/* Interpreta la stringa ricevuta con WINQ_MSG_MAPPA_IMPARA: "<tipo>
 * <percorso>", where <type> is "touch" or "joystick" e <percorso> e' tutto cio'
 * che segue il primo spazio (fino alla fine: un percorso puo' contenere
 * spazi, quindi non si spezza con uno sscanf a campi).
 *
 * La stringa e' un passaggio di PROPRIETA' (g_strdup da chi posta, in
 * winq-ciclo.c) e va liberata qui in OGNI ramo, compresi quelli di errore:
 * altrimenti un comando malformato perde memoria a ogni tentativo.
 *
 * Riceve h per la stessa ragione di WINQ_MSG_MAPPA_CARICA e
 * WINQ_MSG_MAPPA_SPEGNI: deve poter chiamare winq_mappa_accoda_azioni per
 * rilasciare i diti prima di spegnere la mappa, vedi il commento sopra
 * "winq_mappa_accesa = false" qui sotto. */
static void winq_mappa_gestisci_impara(HWND h, char *arg)
{
    char *spazio = strchr(arg, ' ');
    MappaImparaTipo tipo;
    const char *percorso;

    if (!spazio) {
        error_report("winq: comando \"mappa impara\" malformed: %s", arg);
        g_free(arg);
        return;
    }
    *spazio = '\0';
    percorso = spazio + 1;

    if (!strcmp(arg, "touch")) {
        tipo = MAPPA_IMPARA_TOCCO;
    } else if (!strcmp(arg, "joystick")) {
        tipo = MAPPA_IMPARA_JOYSTICK;
    } else {
        error_report("winq: type \"%s\" unknown in \"mappa impara\"", arg);
        g_free(arg);
        return;
    }

    if (!winq_mappa) {
        winq_mappa = mappa_crea();
    }
    /* Il modo impara MISURA il raggio del joystick da due clic, e senza le
     * proporzioni dell'area cliente lo misurerebbe mescolando permille di
     * larghezza e permille di altezza: lo stesso gesto darebbe raggi quasi
     * doppi a seconda della direzione. Vedi winq_mappa_aggiorna_proporzioni. */
    winq_mappa_aggiorna_proporzioni(h);
    /* Un modo impara precedente non concluso si chiude qui, non si accumula:
     * vedi il commento su winq_mappa_impara_chiudi. Chiudendolo rimette anche
     * winq_mappa_accesa com'era prima di quel modo impara, ed e' per questo
     * che lo stato si salva DOPO questa riga e non prima: salvarlo prima
     * vorrebbe dire salvare lo "spenta" imposto dal modo impara precedente e
     * perdere per sempre lo stato vero. */
    winq_mappa_impara_chiudi();

    if (!mappa_impara_inizia(winq_mappa, tipo)) {
        /* mappa_impara_inizia ritorna false, SENZA entrare in modo impara,
         * quando si chiede il joystick e uno fra W, A, S, D e' gia' un
         * tocco. Si scrive nel registro invece di ignorarlo: senza questa
         * riga l'utente entrerebbe in un modo impara che non e' partito, e i
         * clic successivi non farebbero niente senza che nulla lo dica. */
        error_report("winq: learn mode not started: the joystick would use a "
                     "key (W, A, S or D) already assigned to a touch");
        g_free(arg);
        return;
    }

    /* Entrare in modo impara SPEGNE la mappa (riga sotto), e uno spegnimento
     * senza rilascio lascia un dito incollato esattamente come lo lascerebbe
     * WINQ_MSG_MAPPA_SPEGNI senza questa stessa chiamata: se un tasto mappato
     * e' giu' quando arriva "mappa impara ..." (il BEGIN e' gia' partito per
     * il guest), senza questo rilascio winq_mappa_accesa diventa false SENZA
     * un END, il successivo WM_KEYUP di quel tasto trova la mappa spenta e
     * winq_mappa_intercetta ritorna false: il tasto arriva al guest come
     * lettera e il dito resta appoggiato fino al prossimo "carica" o
     * "spegni". Stesso schema di WINQ_MSG_MAPPA_CARICA e
     * WINQ_MSG_MAPPA_SPEGNI qui sotto, apposta: i tre punti in cui la mappa
     * si spegne devono rilasciare allo stesso modo, o resta un quarto modo di
     * spegnerla che non lo fa.
     *
     * Il tasto che si sta ancora tenendo giu' non puo' rientrare qui sotto
     * come ripetizione automatica scambiata per l'assegnazione: nel ramo
     * WM_KEYDOWN/WM_SYSKEYDOWN del modo impara (vedi piu' sotto in questo
     * file) un WM_KEYDOWN con bit 30 acceso e' scartato PRIMA di arrivare a
     * mappa_impara_tasto, e bit 30 resta acceso per ogni ripetizione di un
     * tasto tenuto giu' con continuita' dal sistema operativo, a prescindere
     * da quando questa funzione gira. Rilasciare i diti qui basta: non serve
     * un secondo scarto della ripetizione automatica in questa funzione. */
    {
        /* winq_mappa qui non e' mai NULL: mappa_crea() e' appena girata
         * qualche riga sopra se serviva, esattamente come in
         * WINQ_MSG_MAPPA_CARICA, quindi nessuna guardia su winq_mappa,
         * coerente con quel ramo. */
        MappaAzione az[MAPPA_AZIONI_MAX];
        int n = mappa_rilascia_tutto(winq_mappa, az, MAPPA_AZIONI_MAX);

        winq_mappa_accoda_azioni(h, az, n);
    }
    /* Lo stato che il modo impara dovra' RIMETTERE quando finisce: vedi
     * winq_mappa_accesa_prima_impara. */
    winq_mappa_accesa_prima_impara = winq_mappa_accesa;
    winq_mappa_accesa = false;
    winq_mappa_impara_attiva = true;
    winq_mappa_impara_tipo = tipo;
    winq_mappa_impara_percorso = g_strdup(percorso);
    info_report("winq: learn mode started (%s), waiting for %s", arg,
               tipo == MAPPA_IMPARA_TOCCO ? "a key and then a click"
                                          : "two clicks (centre, then edge)");
    g_free(arg);
}

static LRESULT CALLBACK winq_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        /* winq.geom e il riarmo della catena di scambio NON si toccano qui:
         * li applica il drenaggio, sul ciclo principale. winq.geom ha un solo
         * scrittore -- se lo scrivessero due thread, il tocco leggerebbe una
         * geometria a meta' e cadrebbe spostato di qualche decina di pixel, in
         * silenzio. E winq_present_nuova_dimensione tocca lo stato della
         * presentazione, che vive di la'. */
        winq_accoda_dimensione(LOWORD(lp), HIWORD(lp));
        /* La mappa vive su QUESTO thread, quindi qui si puo' toccare senza
         * lucchetti -- e va toccata: il raggio del joystick e' in permille del
         * lato corto, e con proporzioni vecchie lo stick diventa un'ellisse
         * dopo il primo trascinamento del bordo. Vedi
         * winq_mappa_aggiorna_proporzioni. */
        winq_mappa_aggiorna_proporzioni(h);
        return 0;
    case WM_DPICHANGED: {
        /* LA FINESTRA E' PASSATA SU UNO SCHERMO CON SCALA DIVERSA.
         *
         * lParam porta il rettangolo SUGGERITO da Windows: si applica quello
         * invece di calcolarlo, perche' e' l'unico che tiene conto anche della
         * posizione relativa fra i due monitor.
         *
         * IL WM_SIZE che SetWindowPos manda, QUANDO lo manda, passa dal ramo
         * qui sopra: aggiorna winq.geom, alza winq.expose e azzera il
         * chiavistello del riarmo con winq_present_nuova_dimensione(). Quel
         * percorso e' gia' stato pagato dal task del ridimensionamento, e
         * questo ramo lo riusa di peso.
         *
         * "WHEN IT SENDS IT" non e' sempre: se il rettangolo suggerito ha gia'
         * la stessa dimensione e posizione della finestra, SetWindowPos non
         * manda nessun WM_SIZE, e quella catena non parte. QUI SOTTO L'AREA
         * CLIENTE SI RILEGGE COMUNQUE e si accoda, e non e' ridondanza:
         * SetWindowPos scrive il rettangolo ESTERNO, mentre winq.geom contiene
         * l'area CLIENTE, e un cambio di DPI a rettangolo esterno invariato
         * cambia lo spessore della cornice -- MISURATO su questa macchina,
         * cornice 16x39 a 96 DPI contro 26x71 a 192. Senza rileggere, un
         * passaggio fra due monitor la cui scala facesse coincidere il
         * rettangolo suggerito con quello attuale lascerebbe
         * winq.geom.win_w/win_h vecchi: il DISEGNO si autocorregge da solo (il
         * viewport rilegge la superficie), ma il TOCCO no, perche'
         * winq_win_to_guest conosce solo winq.geom -- il sintomo sarebbe un
         * tocco spostato di alcune decine di pixel, in silenzio,
         * diagnosticabile solo confrontando disegno e tocco.
         *
         * winq_accoda_dimensione porta con se' anche l'"esponi", quindi il
         * ridisegno non dipende da quel caso. Se il WM_SIZE arriva davvero, i
         * due record si sommano: e' innocuo, perche' l'esponi e' un flag e non
         * un contatore, e la dimensione applicata due volte e' la stessa.
         *
         * IL LIMITE, che non si nasconde: questo ramo NON e' verificabile per
         * davvero su questa macchina. Serve un secondo monitor con scala diversa,
         * e non c'e'. Si puo' mandare WM_DPICHANGED a mano con un rettangolo di
         * prova e vedere che la geometria venga applicata -- ma non e' un
         * passaggio reale: Windows non cambia il DPI del monitor sotto la
         * finestra, quindi GetDpiForWindow continua a dire lo stesso valore, e
         * il riarmo della catena di scambio di ANGLE su una transizione vera
         * resta non provato. Dichiarato in qemu/HOST-WINDOW.md. */
        const RECT *r = (const RECT *)lp;
        RECT cliente;

        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left,
                     r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        GetClientRect(h, &cliente);
        winq_accoda_dimensione(cliente.right, cliente.bottom);
        info_report("winq: DPI changed to %u, applied the suggested geometry "
                    "%ldx%ld", (unsigned)LOWORD(wp), r->right - r->left,
                    r->bottom - r->top);
        return 0;
    }
    case WM_PAINT: {
        /* BeginPaint/EndPaint e non solo il flag: WM_PAINT resta in coda
         * finche' la regione non e' validata, e PeekMessage lo ripescherebbe
         * a ogni giro -- la pompa messaggi girerebbe a vuoto per sempre. */
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        winq_accoda_semplice(WINQ_REC_ESPONI);
        return 0;
    }
    case WM_CLOSE:
        /* CHI SPEGNE, E PERCHE' NON PIU' NOI DA SOLI.
         *
         * Prima questo ramo chiamava qmp_quit(NULL) -- l'uscita dura, come fa
         * ui/gtk.c in gd_window_close(). Il difetto e' che data.img e' montato
         * cache=writeback: uscire duro equivale a STACCARE LA CORRENTE, e la
         * partizione dati di Android puo' corrompersi senza che nessuno lo noti.
         *
         * Con il guscio in ascolto si segnala e si aspetta: lui dice ad Android
         * di spegnersi con "reboot -p" -- misurato: la seriale chiude con
         * "reboot: Power down" e QEMU esce da se' in 7 s -- e noi non usciamo
         * affatto, perche' uscirebbe il guest.
         *
         * Senza guscio si fa come prima: chi lancia QEMU a mano deve continuare
         * a poter chiudere la finestra. */
        /* COSA CAMBIA CON IL THREAD. Il segnale al guscio e ShowWindow sono
         * Win32 puro e restano qui, sul thread che possiede la finestra. La
         * scadenza e' un timer di QEMU e qmp_quit vuole il BQL: vanno
         * entrambi al ciclo principale, quindi si accoda l'ESITO del segnale
         * e decide lui quale delle due strade prendere. */
        {
            WinqRec r;

            memset(&r, 0, sizeof(r));
            r.tipo = WINQ_REC_CHIUSURA;
            r.guscio_ha_risposto = winq_segnala_chiusura();
            r.inatteso = false;
            if (r.guscio_ha_risposto) {
                ShowWindow(h, SW_HIDE);
            }
            winq_accoda(&r);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;   /* lo sfondo lo disegna la presentazione, niente sfarfallio */

    /* La tastiera. WM_SYSKEYDOWN/UP e non solo WM_KEYDOWN/UP: i primi arrivano
     * quando Alt e' gia' giu' (Alt+Tab, Alt+F4, il tasto Alt stesso), e senza
     * intercettarli il guest non vedrebbe mai quelle combinazioni. Tradurre
     * qui e restituire 0 (invece di lasciar cadere a DefWindowProc) evita
     * anche che Windows apra il menu di sistema della finestra su Alt da
     * solo, che altrimenti ruberebbe il fuoco alla finestra del guest. */
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        /* Il modo impara per un TOCCO aspetta il tasto da assegnare prima
         * del clic: qui, e non nella mappa gia' attiva (winq_mappa_accesa e'
         * spenta apposta durante il modo impara, quindi winq_mappa_intercetta
         * qui sotto ritornerebbe comunque false). Il joystick non passa da
         * qui: usa sempre W, A, S, D per costruzione (mappa_impara_punto), e
         * non c'e' nessun tasto da assegnare a mano. */
        if (winq_mappa_impara_attiva &&
            winq_mappa_impara_tipo == MAPPA_IMPARA_TOCCO) {
            /* Bit 30 di LPARAM: ripetizione automatica. Si scarta come ogni
             * altro tasto in questo file, o tenerlo giu' richiamerebbe
             * mappa_impara_tasto a raffica -- innocuo perche' riscrive lo
             * stesso vk, ma inutile. */
            if (!(lp & (1 << 30)) && !mappa_impara_tasto(winq_mappa, (int)wp)) {
                warn_report_once("winq: key not valid for learn mode "
                                 "(vk=%u)", (unsigned)wp);
            }
            return 0;
        }
        if (!winq_mappa_intercetta(h, wp, lp, true)) {
            winq_accoda_tasto(wp, lp, true);
        }
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (winq_mappa_impara_attiva &&
            winq_mappa_impara_tipo == MAPPA_IMPARA_TOCCO) {
            /* Il rilascio non ha niente da fare nel modo impara -- il tasto
             * e' stato preso al KEYDOWN -- ma va comunque consumato qui: non
             * chiamare winq_accoda_tasto vuol dire non farlo mai arrivare al
             * guest come lettera. */
            return 0;
        }
        if (!winq_mappa_intercetta(h, wp, lp, false)) {
            winq_accoda_tasto(wp, lp, false);
        }
        return 0;

    /* La finestra perde il fuoco a tasti premuti: senza questo, il guest
     * resta con un dito incollato e il gioco continua a camminare da solo.
     * Windows non manda il WM_KEYUP di un tasto premuto mentre il fuoco se ne
     * va. */
    case WM_KILLFOCUS: {
        MappaAzione az[MAPPA_AZIONI_MAX];
        int n;

        if (winq_mappa) {
            n = mappa_rilascia_tutto(winq_mappa, az, MAPPA_AZIONI_MAX);
            winq_mappa_accoda_azioni(h, az, n);
        }
        return 0;
    }

    /* La rotella. GET_WHEEL_DELTA_WPARAM da' un valore CON SEGNO che sui
     * touchpad di precisione e' minore di 120: l'accumulo e' dentro
     * winq_rotella, e il perche' del mittente NULL sta nel commento di quella
     * funzione (winq-input.c). */
    case WM_MOUSEWHEEL: {
        WinqRec r;

        memset(&r, 0, sizeof(r));
        r.tipo = WINQ_REC_ROTELLA;
        r.delta = GET_WHEEL_DELTA_WPARAM(wp);
        winq_accoda(&r);
        return 0;
    }

    /* Il tocco. WM_POINTER* e non WM_TOUCH: la famiglia WM_POINTER* e' quella
     * che porta un identificativo per dito senza chiedere RegisterTouchWindow
     * ne' GetTouchInputInfo, e nessuna registrazione serve -- da Windows 8 il
     * tocco arriva come WM_POINTER* a qualunque finestra che non abbia chiesto
     * esplicitamente i vecchi WM_TOUCH. Le dichiarazioni ci sono perche'
     * qemu/osdep.h:82 fissa _WIN32_WINNT a 0x0602.
     *
     * NON si usano i messaggi di mouse promossi dal tocco: sono un solo
     * puntatore per costruzione, quindi il multitouch andrebbe perso, e non
     * distinguono un dito da un cursore. Il mouse come dito (una verifica del
     * piano) NON passa da qui: e' il ramo WM_LBUTTONDOWN/WM_MOUSEMOVE/
     * WM_LBUTTONUP piu' sotto, apposta separato da questo, per non dover
     * riaprire il filtro sul tipo di puntatore due righe sotto ne' chiamare
     * EnableMouseInPointer (che avrebbe fuso i due percorsi e ricreato
     * proprio il rischio di cui parla il filtro). Vedi il commento su quel
     * ramo per il perche' in dettaglio. */
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP: {
        UINT32 pid = GET_POINTERID_WPARAM(wp);
        POINTER_INFO pi;
        POINT p;
        /* int e non InputMultiTouchType: da qui in poi il tipo del tocco viaggia
         * come numero dentro un record (WINQ_TOCCO_*, winq-coda.h), e l'enum di
         * QEMU non entra in questo file. Che i tre numeri combacino con quelli di
         * QEMU e' verificato a COMPILAZIONE in winq-ciclo.c. */
        int t;

        if (!GetPointerInfo(pid, &pi)) {
            /* Il puntatore e' gia' sparito dalle strutture di Windows: non c'e'
             * posizione da tradurre.
             *
             * Ma su un WM_POINTERUP il dito va staccato COMUNQUE. Trovato dalla
             * review finale: lasciar cadere a DefWindowProc qui significa non
             * emettere mai l'END, e Android resta convinto che il dito sia
             * appoggiato -- esattamente il guasto che WM_POINTERCAPTURECHANGED
             * piu' sotto e' stato aggiunto per prevenire, e che quel ramo evita
             * proprio NON chiamando GetPointerInfo, perche' per un rilascio la
             * posizione non serve. I due gestori si contraddicevano.
             *
             * winq_handle_pointer_event riusa l'ultima posizione nota dello slot,
             * quindi le coordinate passate qui sono ignorate. */
            if (msg == WM_POINTERUP) {
                winq_accoda_puntatore(pid, 0, 0, WINQ_TOCCO_END, false);
                return 0;
            }
            break;
        }

        /* SOLO dito e penna diventano tocco. MISURATO, non previsto: su questa
         * macchina arrivavano WM_POINTERUPDATE con POINTER_FLAG_INCONTACT
         * acceso mentre nessuno toccava lo schermo -- traiettorie curve e
         * continue, chiaramente non le nostre iniezioni -- e finivano al guest
         * come trascinamenti del dito. Sono i puntatori del touchpad di
         * precisione (PT_TOUCHPAD) e del mouse (PT_MOUSE), che nella famiglia
         * WM_POINTER* passano per gli stessi messaggi del tocco.
         *
         * Senza questo filtro il sintomo e' Android che si muove da solo, e
         * cercarlo nella mappatura o negli slot sarebbe cercarlo nel posto
         * sbagliato. Il mouse come dito e' una scelta da fare esplicitamente,
         * col suo criterio, non un effetto collaterale. */
        if (pi.pointerType != PT_TOUCH && pi.pointerType != PT_PEN) {
            if (winq_diag_tocco()) {
                info_report("winq touch: pointer of type %u dropped "
                            "(expected PT_TOUCH=%d or PT_PEN=%d)",
                            (unsigned)pi.pointerType, PT_TOUCH, PT_PEN);
            }
            break;
        }

        if (msg == WM_POINTERDOWN) {
            t = WINQ_TOCCO_BEGIN;
        } else if (msg == WM_POINTERUP) {
            t = WINQ_TOCCO_END;
        } else {
            /* Non ogni WM_POINTERUPDATE e' un dito che striscia: una penna che
             * passa sopra il vetro senza toccarlo, e su alcuni digitalizzatori
             * anche un dito vicino, generano aggiornamenti con
             * POINTER_FLAG_INCONTACT spento. Tradurli sarebbe un trascinamento
             * a dito alzato -- il guasto opposto e simmetrico a quello che
             * questo task risolve. */
            if (!(pi.pointerFlags & POINTER_FLAG_INCONTACT)) {
                return 0;
            }
            t = WINQ_TOCCO_UPDATE;
        }

        /* ptPixelLocation e' in coordinate di SCHERMO, non della finestra:
         * senza ScreenToClient ogni tocco cadrebbe spostato della posizione
         * della finestra sul desktop, e il sintomo -- un tocco che arriva
         * altrove -- somiglia a un errore di mappatura invece che a
         * un'origine sbagliata. */
        p = pi.ptPixelLocation;
        ScreenToClient(h, &p);

        /* winq_slot_per_pointer NON si chiama piu' da qui: il record porta
         * l'identificativo di Windows cosi' com'e', e la mappatura su slot la fa
         * il drenaggio. E' cosi' che lo stato degli slot resta interamente sul
         * ciclo principale invece di essere toccato da due thread. */
        winq_accoda_puntatore(pid, p.x, p.y, t, true);
        return 0;
    }

    case WM_POINTERCAPTURECHANGED:
        /* La cattura del puntatore ci e' stata portata via (gesto di sistema,
         * barra delle applicazioni, un'altra finestra): per QUESTO dito non
         * arrivera' nessun WM_POINTERUP. Senza chiudere il tocco qui, Android
         * resterebbe convinto che il dito e' ancora appoggiato: lo stesso
         * sintomo, e la stessa difficolta' di diagnosi, dell'asimmetria di
         * BTN_TOUCH descritta in winq-input.c.
         *
         * Non si chiama GetPointerInfo: il puntatore puo' essere gia' fuori
         * dalle strutture di Windows, e per un rilascio la posizione non serve
         * (winq_handle_pointer_event riusa l'ultima nota). */
        winq_accoda_puntatore(GET_POINTERID_WPARAM(wp), 0, 0, WINQ_TOCCO_END,
                              false);
        return 0;

    /* Il mouse come dito solo. WM_LBUTTONDOWN/WM_MOUSEMOVE/
     * WM_LBUTTONUP, non WM_POINTER*: e' un percorso separato apposta, per non
     * toccare il filtro sul tipo di puntatore qui sopra. Quel filtro esiste
     * perche' MISURATO che PT_MOUSE (e PT_TOUCHPAD) su WM_POINTER* produce
     * trascinamenti fantasma -- Android che si muove da solo -- e
     * l'alternativa comune, EnableMouseInPointer, farebbe *rientrare* il
     * mouse in quello stesso ramo con quello stesso pointerType, ricreando
     * esattamente il guasto che il filtro serve a evitare. I messaggi
     * WM_LBUTTONDOWN, WM_MOUSEMOVE e WM_LBUTTONUP invece non sono mai
     * instradati sul filtro: nessun rischio di riaprirlo per sbaglio.
     *
     * REGOLA CENTRALE, quella che questo task non deve rompere: muovere il
     * mouse senza il pulsante premuto non deve MAI diventare un dito che
     * striscia. Per questo WM_MOUSEMOVE traduce un evento SOLO quando
     * MK_LBUTTON e' acceso in wParam; senza pulsante non si chiama
     * winq_handle_pointer_event, punto (non un "return 0" cosmetico: proprio
     * nessuna chiamata).
     *
     * WINQ_MOUSE_ID e' un identificativo inventato, mai un vero pointer id
     * di Windows: quelli sono assegnati da Windows in sequenza a partire da
     * numeri piccoli (visti in pratica sotto qualche migliaio), quindi
     * 0x7fffffff non collide in pratica. Si passa a winq_slot_per_pointer
     * esattamente come farebbe un dito vero -- quella funzione non guarda
     * altro che l'uguaglianza dell'id passato -- quindi il mouse riusa di
     * peso lo stesso percorso di slot e la stessa winq_handle_pointer_event
     * gia' verificata dal tocco (37 pressioni / 37 rilasci): zero
     * stato nuovo, zero logica di traduzione duplicata.
     *
     * SetCapture/ReleaseCapture: senza cattura, rilasciare il pulsante fuori
     * dal rettangolo della finestra (si trascina, si esce con lo schermo
     * ancora giu', si rilascia fuori) manderebbe quel WM_LBUTTONUP alla
     * finestra sotto il cursore in quel momento -- non la nostra -- e questo
     * wndproc non lo vedrebbe mai: Android crederebbe il dito ancora
     * appoggiato per sempre. Stessa classe di guasto, e stesso rimedio, di
     * WM_POINTERCAPTURECHANGED qui sopra per il tocco vero; WM_CAPTURECHANGED
     * e' il suo equivalente per il mouse, per quando la cattura ci viene
     * tolta da un gesto di sistema (Alt+Tab, un dialogo modale) mentre il
     * pulsante e' ancora giu'. */
#define WINQ_MOUSE_ID 0x7fffffff
    case WM_LBUTTONDOWN:
        /* Il clic del modo impara: consuma il clic invece di iniettarlo come
         * dito. E' il giro inverso di winq_mappa_pm_a_cliente (vedi
         * winq_mappa_cliente_a_pm), e non passa da SetCapture -- non c'e'
         * nessun trascinamento da seguire, solo un punto. */
        if (winq_mappa_impara_attiva) {
            int x_pm, y_pm;
            char err[128];

            winq_mappa_cliente_a_pm(h, (int)(short)LOWORD(lp),
                                    (int)(short)HIWORD(lp), &x_pm, &y_pm);
            /* TRE esiti e non due, perche' due li confondevano: quando i
             * tocchi erano gia' MAPPA_TOCCHI_MAX il modulo chiudeva il modo
             * impara e ritornava false, questo ramo lo leggeva come "serve un
             * altro clic" e lasciava winq_mappa_impara_attiva acceso. Da li'
             * in poi i due stati divergevano -- ogni clic nel vuoto, la mappa
             * spenta, nessuna riga di registro -- e l'unica uscita era una
             * scorciatoia che l'utente non ha motivo di provare. */
            switch (mappa_impara_punto(winq_mappa, x_pm, y_pm)) {
            case MAPPA_ESITO_FATTO:
                /* L'assegnazione e' COMPLETA (per il tocco al primo clic, per
                 * il joystick al secondo). Si scrive su file e si esce dal modo
                 * impara -- ANCHE se mappa_salva fallisce: il modo impara si e'
                 * comunque concluso in memoria, e restare in quello stato in
                 * silenzio sarebbe un secondo guasto sopra il primo. */
                if (mappa_salva(winq_mappa, winq_mappa_impara_percorso,
                                err, sizeof(err))) {
                    info_report("winq: key map saved to %s",
                                winq_mappa_impara_percorso);
                } else {
                    error_report("winq: map not saved (%s): %s",
                                 winq_mappa_impara_percorso, err);
                }
                /* LA RIDUZIONE DEL RAGGIO VA DETTA, accanto al "salvata".
                 * Il modulo la applica in silenzio per non scrivere un
                 * joystick che poi rifiuterebbe di rileggere, e il caso
                 * frequente non e' vistoso: lo stick di un gioco sta in un
                 * ANGOLO, li' il massimo consentito e' piccolo, quindi lo
                 * stick funziona ma "tira" a una distanza diversa da quella
                 * appena tracciata col mouse. Senza questa riga l'utente
                 * rifarebbe lo stesso gesto convinto di aver sbagliato lui. */
                {
                    int misurato = 0, usato = 0;

                    if (mappa_impara_raggio(winq_mappa, &misurato, &usato) &&
                        usato != misurato) {
                        warn_report("winq: joystick radius reduced from %d to "
                                    "%d per mille: the circle did not fit "
                                    "in the area", misurato, usato);
                    }
                }
                /* winq_mappa_impara_azzera e non winq_mappa_impara_chiudi:
                 * mappa_impara_punto ha appena detto FATTO, cioe' il modulo ha
                 * GIA' chiuso il modo impara da se'. Richiamare
                 * winq_mappa_impara_chiudi chiamerebbe anche
                 * mappa_impara_annulla, annullando l'assegnazione appena
                 * riuscita invece di limitarsi a liberare la copia locale del
                 * percorso e a rimettere lo stato acceso/spento. */
                winq_mappa_impara_azzera();
                break;
            case MAPPA_ESITO_ANCORA:
                /* Il modo impara resta aperto. Si SCRIVE cosa manca: un clic
                 * che non produce niente e non dice niente sembra un guasto, e
                 * nel caso del tocco senza tasto non c'era proprio nessun
                 * messaggio. Quale dei due manchi lo sa questo file, che
                 * conosce il tipo; il modulo no. */
                if (winq_mappa_impara_tipo == MAPPA_IMPARA_TOCCO) {
                    info_report("winq: learn mode: press the key to assign FIRST, "
                                "assign, then click the point");
                } else {
                    info_report("winq: learn mode: centre taken, now "
                                "click a point on the edge of the stick");
                }
                break;
            case MAPPA_ESITO_FALLITO:
                /* Il modulo ha chiuso il modo impara senza scrivere niente. Si
                 * chiude anche QUI, o i due stati divergono.
                 *
                 * IL MOTIVO DIPENDE DAL TIPO, e dirne uno solo manderebbe
                 * l'utente a cercare il guasto dalla parte sbagliata: sul
                 * joystick la mappa piena non c'entra niente (il joystick e'
                 * uno solo e non consuma tocchi), il fallimento e' il raggio
                 * che verrebbe zero -- centro sul bordo, o due clic nello
                 * stesso punto. */
                if (winq_mappa_impara_tipo == MAPPA_IMPARA_JOYSTICK) {
                    error_report("winq: learn mode not finished: the radius "
                                 "of the joystick would be zero (centre on the "
                                 "edge, or two clicks on the same point)");
                } else {
                    error_report("winq: learn mode not finished: the map already has "
                                 "already %d touches, the maximum",
                                 MAPPA_TOCCHI_MAX);
                }
                winq_mappa_impara_azzera();
                break;
            }
            return 0;
        }
        SetCapture(h);
        winq_accoda_puntatore(WINQ_MOUSE_ID, (int)(short)LOWORD(lp),
                              (int)(short)HIWORD(lp), WINQ_TOCCO_BEGIN, true);
        return 0;
    case WM_MOUSEMOVE:
        if (wp & MK_LBUTTON) {
            winq_accoda_puntatore(WINQ_MOUSE_ID, (int)(short)LOWORD(lp),
                                  (int)(short)HIWORD(lp), WINQ_TOCCO_UPDATE,
                                  true);
        }
        return 0;
    case WM_LBUTTONUP:
        /* QUESTO RAMO NON STACCA MAI IL DITO, ED E' GIUSTO COSI'.
         *
         * Il commento che stava qui diceva il contrario, e la review finale l'ha
         * smentito COMPILANDO un programma Win32 di prova: ReleaseCapture()
         * consegna WM_CAPTURECHANGED in modo SINCRONO, dentro questa stessa
         * chiamata. Quando si torna dalla riga qui sotto il dito e' quindi gia'
         * stato chiuso dal ramo WM_CAPTURECHANGED, e winq_slot_per_pointer
         * trovera' lo slot liberato: ritorna INPUT_EVENT_SLOTS_MAX e la chiamata
         * seguente scarta.
         *
         * Si tiene comunque, per il caso in cui la cattura non ci fosse mai stata
         * -- SetCapture puo' fallire -- perche' allora nessun WM_CAPTURECHANGED
         * arriva e questo diventa l'unica via d'uscita.
         *
         * PERCHE' IL COMMENTO SBAGLIATO ERA UN DIFETTO E NON UN DETTAGLIO: chi
         * leggesse WM_CAPTURECHANGED credendolo la rete di sicurezza di questo
         * ramo, e lo rimuovesse in un riordino, spegnerebbe il mouse in silenzio.
         * Il vero rapporto fra i due e' l'opposto. */
        ReleaseCapture();
        winq_accoda_puntatore(WINQ_MOUSE_ID, (int)(short)LOWORD(lp),
                              (int)(short)HIWORD(lp), WINQ_TOCCO_END, true);
        return 0;
    case WM_CAPTURECHANGED:
        /* Vedi il commento sopra: la cattura e' stata tolta mentre il
         * pulsante era giu'. Non si chiama GetCapture ne' si guarda lp
         * (l'HWND del nuovo proprietario, che qui non interessa): l'unica
         * cosa da fare e' chiudere il dito del mouse se ce n'era uno aperto.
         * winq_slot_per_pointer con END su un id non attivo ritorna
         * INPUT_EVENT_SLOTS_MAX e winq_handle_pointer_event scarta, quindi
         * questo ramo e' innocuo anche quando non c'era nessun trascinamento
         * in corso. */
        winq_accoda_puntatore(WINQ_MOUSE_ID, 0, 0, WINQ_TOCCO_END, false);
        return 0;

    case WINQ_MSG_DIMENSIONA:
        /* IL CICLO PRINCIPALE CHIEDE, IL PROPRIETARIO ESEGUE. Vedi il commento
         * su WINQ_MSG_DIMENSIONA in winq.h: SetWindowPos consegna WM_SIZE in
         * modo SINCRONO al thread proprietario, quindi chiamarla dal ciclo
         * principale lo bloccherebbe ogni volta che l'utente sta tenendo il
         * bordo -- il guasto che questo lavoro chiude, rifatto sul percorso
         * della rotazione. */
        winq_dimensiona_cliente((int)wp, (int)lp);
        return 0;

    case WINQ_MSG_MAPPA_CARICA: {
        /* lParam e' un passaggio di PROPRIETA': g_strdup da chi posta
         * (winq-ciclo.c), g_free da chi riceve, qui, in OGNI ramo. */
        char *percorso = (char *)lp;
        MappaAzione az[MAPPA_AZIONI_MAX];
        char err[128];
        int riga = 0, n;

        /* Un modo impara in corso non ha piu' senso su una mappa che sta per
         * essere sostituita: si chiude, o il prossimo clic finirebbe a
         * scrivere sul percorso vecchio dentro una mappa che non e' piu'
         * quella su cui l'utente credeva di lavorare. */
        winq_mappa_impara_chiudi();

        if (!winq_mappa) {
            winq_mappa = mappa_crea();
        }
        /* Prima si rilascia: cambiare mappa con un dito giu' lo lascerebbe
         * incollato, perche' la mappa nuova non sa che esiste. */
        n = mappa_rilascia_tutto(winq_mappa, az, MAPPA_AZIONI_MAX);
        winq_mappa_accoda_azioni(h, az, n);

        /* Dopo mappa_crea e prima di usare la mappa: senza le proporzioni il
         * joystick e' un'ellisse (vedi winq_mappa_aggiorna_proporzioni). Le
         * proporzioni sopravvivono a mappa_carica per costruzione, ma
         * impostarle QUI vuol dire che una mappa caricata prima del primo
         * WM_SIZE e' gia' rotonda invece di diventarlo al primo
         * ridimensionamento. */
        winq_mappa_aggiorna_proporzioni(h);

        if (mappa_carica(winq_mappa, percorso, err, sizeof(err), &riga)) {
            winq_mappa_accesa = true;
            info_report("winq: key map loaded from %s", percorso);
        } else {
            winq_mappa_accesa = false;
            error_report("winq: map %s line %d: %s", percorso, riga, err);
        }
        g_free(percorso);
        return 0;
    }

    case WINQ_MSG_MAPPA_SPEGNI: {
        MappaAzione az[MAPPA_AZIONI_MAX];
        int n;

        winq_mappa_impara_chiudi();
        if (winq_mappa) {
            n = mappa_rilascia_tutto(winq_mappa, az, MAPPA_AZIONI_MAX);
            winq_mappa_accoda_azioni(h, az, n);
        }
        winq_mappa_accesa = false;
        return 0;
    }

    case WINQ_MSG_MAPPA_IMPARA:
        /* lParam e' lo stesso passaggio di proprieta' di sopra; lo libera
         * winq_mappa_gestisci_impara, in ogni suo ramo. h passato per lo
         * stesso motivo di WINQ_MSG_MAPPA_CARICA/SPEGNI qui sopra: serve a
         * winq_mappa_accoda_azioni per rilasciare i diti prima di spegnere
         * la mappa. */
        winq_mappa_gestisci_impara(h, (char *)lp);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}


/* TRADURRE UNA DIMENSIONE DI AREA CLIENTE IN UNA DI FINESTRA, in un posto solo.
 *
 * Due chiamanti: la creazione della finestra e il cambio di risoluzione. Prima
 * erano due calcoli distinti, e la consapevolezza del DPI li ha fatti divergere
 * -- uno per-DPI e l'altro col DPI di sistema congelato all'avvio -- il che
 * riapriva all'AVVIO la stessa trappola chiusa sulla rotazione. Uno solo non
 * puo' divergere.
 *
 * Vuole winq.hwnd gia' esistente, perche' il DPI si chiede alla FINESTRA: e' il
 * solo modo di avere la scala del monitor su cui sta davvero, e non quella del
 * primario. */
static void winq_dimensiona_cliente(int w, int h)
{
    RECT r = { 0, 0, w, h };
    DWORD stile = (DWORD)GetWindowLongPtrA(winq.hwnd, GWL_STYLE);
    /* Letto anche se oggi vale sempre 0 (CreateWindowEx(0, ...) piu' sotto):
     * il calcolo del bordo deve tenerne conto comunque, o mentirebbe muto il
     * giorno in cui comparisse un WS_EX_*. */
    DWORD stile_ex = (DWORD)GetWindowLongPtrA(winq.hwnd, GWL_EXSTYLE);

    if (winq_fn_adjust && winq_fn_dpi) {
        UINT dpi = winq_fn_dpi(winq.hwnd);

        /* Zero e' cio' che GetDpiForWindow ritorna per un handle non valido:
         * si ricade sul riferimento invece di propagarlo, o il calcolo del
         * bordo darebbe una cornice assurda. Stessa guardia, stessa ragione,
         * di dpi_di_finestra in app/guscio/dpi.c. */
        if (!dpi) {
            dpi = 96;
        }
        winq_fn_adjust(&r, stile, FALSE, stile_ex, dpi);
    } else {
        /* Senza le funzioni per-DPI il processo non e' nemmeno aware, quindi il
         * DPI di sistema e' l'unica scala in gioco e questa e' la strada giusta:
         * e' lo stesso calcolo di prima di questo lavoro. AdjustWindowRectEx e
         * non AdjustWindowRect: il ramo per-DPI qui sopra usa stile_ex, e i due
         * rami non possono divergere -- vedi il commento sopra questa funzione. */
        AdjustWindowRectEx(&r, stile, FALSE, stile_ex);
    }
    SetWindowPos(winq.hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

bool winq_create_window(int w, int h)
{
    WNDCLASSEX wc = {
        .cbSize = sizeof(wc),
        .lpfnWndProc = winq_wndproc,
        .hInstance = GetModuleHandle(NULL),
        .hCursor = LoadCursor(NULL, IDC_ARROW),
        .lpszClassName = "winq",
        /* CS_OWNDC e' obbligatorio per il percorso WGL, e innocuo per quello EGL.
         * Senza, ogni GetDC puo' restituire un HDC DIVERSO per la stessa finestra, e
         * il formato pixel impostato su uno non vale sull'altro: wglCreateContext
         * fallirebbe su un HDC apparentemente identico a quello che ha funzionato.
         * Con CS_OWNDC la finestra ha un solo HDC per tutta la vita. */
        .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
    };
    if (!RegisterClassEx(&wc)) {
        error_report("winq: RegisterClassEx failed, error %lu", GetLastError());
        return false;
    }

    /* w e h passati cosi' come sono, non attraverso AdjustWindowRect: qui non
     * esiste ancora un HWND a cui chiedere il DPI, quindi qualunque calcolo del
     * bordo userebbe lo stesso DPI di sistema congelato all'avvio che il resto
     * di questo file evita apposta -- non sarebbe una stima migliore, solo la
     * stessa trappola un passo prima. Si crea con questa dimensione (che vale
     * da placeholder) e si corregge SUBITO dopo con winq_dimensiona_cliente,
     * appena l'handle esiste e il DPI vero si puo' chiedere alla finestra. La
     * finestra non e' ancora visibile (ShowWindow viene dopo), quindi questo
     * passaggio intermedio non si vede. */
    winq.hwnd = CreateWindowEx(0, "winq", "Android", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                               NULL, NULL, wc.hInstance, NULL);
    if (!winq.hwnd) {
        error_report("winq: CreateWindowEx failed, error %lu", GetLastError());
        return false;
    }
    winq_dimensiona_cliente(w, h);
    {
        RECT cliente;

        /* winq.geom viene SEMPRE da GetClientRect o da WM_SIZE, mai da un
         * numero scritto a mano: se SetWindowPos dentro winq_dimensiona_cliente
         * non avesse ottenuto la dimensione chiesta, scrivere qui w/h
         * sovrascriverebbe il valore vero (quello del WM_SIZE gia' passato)
         * con quello desiderato. */
        GetClientRect(winq.hwnd, &cliente);
        /* UNICO PUNTO IN CUI IL THREAD DELLA FINESTRA SCRIVE winq.geom, e non
         * contraddice la regola dello scrittore unico: qui il ciclo principale
         * e' FERMO dentro qemu_sem_wait, dentro winq_avvia_thread, e riprendera'
         * solo dopo il qemu_sem_post che segue questa scrittura -- il semaforo e'
         * anche la barriera che la rende visibile di la'. Da qui in poi winq.geom
         * lo scrive soltanto il drenaggio. */
        winq.geom.win_w = cliente.right;
        winq.geom.win_h = cliente.bottom;
    }
    ShowWindow(winq.hwnd, SW_SHOW);
    return true;
}

/* IL THREAD DELLA FINESTRA.
 *
 * Una finestra Win32 appartiene al thread che l'ha CREATA: il suo wndproc gira
 * sempre li', e li' gira anche il ciclo modale in cui Windows entra dentro
 * DefWindowProc quando l'utente afferra un bordo o la barra del titolo. Creare
 * la finestra qui e' quindi tutto cio' che serve perche' quel ciclo modale non
 * possa piu' fermare il ciclo principale di QEMU, e con lui la macchina
 * virtuale.
 *
 * IL SEMAFORO NON E' PRUDENZA: winq_present_init deve creare il contesto GL
 * sull'HWND, quindi winq_init non puo' proseguire finche' la finestra non
 * esiste. E porta il FALLIMENTO oltre al successo, o un CreateWindowEx fallito
 * lascerebbe il ciclo principale ad aspettare per sempre -- che e' peggio del
 * guasto che segnala. */
static QemuThread winq_thread;
static QemuSemaphore winq_pronta;
static bool winq_finestra_creata;
static int winq_iniziale_w, winq_iniziale_h;

/* WINQ_TRACCIA_AVVIO=1: briciole sul thread della finestra.
 *
 * PERCHE' ESISTE. Con una opengl32.dll costruita in casa (Mesa 26.2.0-rc3,
 * research/mesa-src) QEMU esce con codice 87 subito dopo winq_init, muto.
 * Il resto e' gia' escluso, ognuno con una prova: non virglrenderer (esce 87
 * anche con virtio-gpu-pci), non Venus, non la forma della DLL (monolitica
 * come quella spedita), e nemmeno la DLL in se' -- con -display none QEMU
 * gira. E il battito non parte mai: le briciole in winq-ciclo.c non stampano
 * nulla. Resta questo thread, l'unica altra cosa nostra viva in quel momento.
 *
 * Gemella di winq_traccia_avvio() in winq-ciclo.c: duplicata invece che
 * esportata perche' e' lo strumento di una diagnosi in corso, non un'API, e un
 * header nuovo per due righe costa piu' di quanto renda. */
static void winq_traccia_finestra(const char *dove)
{
    static int acceso = -1;

    if (acceso < 0) {
        const char *v = getenv("WINQ_TRACCIA_AVVIO");

        acceso = (v && *v == '1') ? 1 : 0;
    }
    if (acceso) {
        info_report("winq-traccia finestra: %s", dove);
    }
}

static void *winq_thread_corpo(void *opaque)
{
    MSG m;
    BOOL r;

    winq_traccia_finestra("thread partito");
    winq_finestra_creata = winq_create_window(winq_iniziale_w, winq_iniziale_h);
    winq_traccia_finestra(winq_finestra_creata ? "finestra creata"
                                               : "finestra NON creata");
    qemu_sem_post(&winq_pronta);
    if (!winq_finestra_creata) {
        return NULL;
    }

    /* GetMessage e non PeekMessage, ed e' la differenza che rende questo thread
     * quasi gratuito: dorme finche' un messaggio non arriva, invece di girare a
     * vuoto ogni cinque millisecondi come faceva la pompa dentro il battito. */
    winq_traccia_finestra("entro nel ciclo dei messaggi");
    while ((r = GetMessage(&m, NULL, 0, 0)) != 0) {
        if (r == -1) {
            error_report("winq: GetMessage failed (%lu), the window "
                         "window exits", GetLastError());
            break;
        }
        TranslateMessage(&m);
        DispatchMessage(&m);
    }

    /* r == 0 e' WM_QUIT, cioe' una chiusura CHIESTA; r == -1 e' un errore di
     * GetMessage. Distinguerli e' il punto della diagnosi: hanno cause
     * opposte, e da fuori si vedono uguali -- QEMU che sparisce in silenzio. */
    winq_traccia_finestra(r == 0
                          ? "ciclo finito: WM_QUIT, chiusura chiesta"
                          : "ciclo finito: GetMessage in errore");

    /* SI ARRIVA QUI SOLO PER GUASTO, e va detto forte. Una macchina virtuale
     * viva senza finestra e' il guasto peggiore: QEMU invisibile, chiudibile
     * solo dal Task Manager. E' lo stesso ragionamento di
     * winq_scadenza_scattata, in winq-ciclo.c.
     *
     * Non si chiama qmp_quit da qui: vuole il BQL, e la regola di questo file e'
     * che non si chiama niente di QEMU. Si accoda, e il ciclo principale esce. */
    {
        WinqRec fine;

        memset(&fine, 0, sizeof(fine));
        fine.tipo = WINQ_REC_CHIUSURA;
        fine.guscio_ha_risposto = false;
        fine.inatteso = true;
        winq_accoda(&fine);
    }
    return NULL;
}

bool winq_avvia_thread(int w, int h)
{
    winq_iniziale_w = w;
    winq_iniziale_h = h;
    /* La coda prima del thread: il wndproc puo' accodare gia' dentro
     * CreateWindowEx, che consegna WM_CREATE e WM_SIZE in modo sincrono prima
     * ancora di ritornare l'handle. */
    winq_coda_init();
    qemu_sem_init(&winq_pronta, 0);
    /* DETACHED: nessuno raccogliera' mai questo thread. Alla chiusura il
     * processo esce, e aspettarlo vorrebbe dire aggiungere un'attesa sul
     * percorso di uscita -- che e' misurato a 4485 ms e ha gia' un margine
     * stretto contro lo spegnimento di Windows. */
    qemu_thread_create(&winq_thread, "winq-finestra", winq_thread_corpo, NULL,
                       QEMU_THREAD_DETACHED);
    qemu_sem_wait(&winq_pronta);
    return winq_finestra_creata;
}

