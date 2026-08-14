/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* finestra.c -- la finestra del guscio: fasi, bottoni hardware, registro.
 *
 * Tre zone dall'alto in basso, e la ragione di ognuna:
 *   le FASI, perche' oggi per cento secondi non si vede nulla e non si
 *     distingue un avvio lento da uno rotto;
 *   i BOTTONI hardware, perche' power, volume, home, back e recenti non hanno
 *     nessun altro modo di essere premuti su un dispositivo senza tasti fisici;
 *   il REGISTRO, perche' quando qualcosa non va la ragione e' quasi sempre in
 *     una riga di seriale che oggi finisce in un file che nessuno apre.
 *
 * PERCHE' UN CONTROLLO EDIT E NON UNA LISTA: il testo si seleziona e si copia
 * gratis, che e' cio' che si vuole fare con una riga di errore. Una ListView
 * sarebbe piu' bella e meno utile. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <shellapi.h>   /* DragAcceptFiles, DragQueryFileA, DragFinish */
#include "guscio.h"

#define FIN_CLASSE   "AndroidRuntimeGuscio"
#define FIN_LARGHEZZA 760
#define FIN_ALTEZZA   520
#define FIN_TIMER     1

/* LE COSTANTI DI POSIZIONE, tutte a 96 DPI e tutte qui. Prima vivevano due
 * volte -- una nella creazione dei controlli e una nel gestore di WM_SIZE -- con
 * valori che gia' non concordavano: la creazione usava FIN_LARGHEZZA - 40 per la
 * larghezza del pannello e WM_SIZE usava w - 20. Con la scala in mezzo due copie
 * diventerebbero due layout diversi a due DPI diversi, e il sintomo (controlli
 * che saltano al primo ridimensionamento) non nominerebbe la causa. Da qui in
 * poi le posizioni le decide fin_disponi e nessun altro. */
#define FIN_MARGINE    10
#define FIN_FASI_Y     10
#define FIN_FASI_H     22
#define FIN_BOT_Y      40
#define FIN_BOT_W      96
#define FIN_BOT_H      28
#define FIN_BOT_PASSO 100
/* Lo spazio verticale fra due righe di bottoni, quando la larghezza non basta
 * per tenerli su una sola. Vedi fin_disponi. */
#define FIN_BOT_GAP     6
/* Il corpo del carattere a 96 DPI. 12 pixel e' l'altezza dei caratteri del
 * Segoe UI a 9 punti, cioe' il carattere di interfaccia predefinito di Windows. */
#define FIN_FONT_PX    12

/* Due costanti DISTINTE apposta: unificarle e' esattamente il guasto che
 * questo commento serve a impedire.
 *
 * FIN_LOG_TETTO e' il tetto RIGIDO imposto al controllo EDIT con
 * EM_SETLIMITTEXT. Un controllo EDIT di Win32 senza limite esplicito accetta
 * di default solo 30000 caratteri: oltre, rifiuta in silenzio ogni
 * EM_REPLACESEL, senza errore. Un avvio di Android produce pero' MOLTO
 * piu' di 30000 caratteri di seriale -- MISURATO: 201344 in un avvio -- da
 * qui la necessita' di alzarlo.
 *
 * FIN_LOG_TRONCA e' la soglia applicativa: quando il testo del pannello la
 * supera, finestra_passo libera spazio dalla testa PRIMA che il tetto del
 * controllo venga raggiunto.
 *
 * Una correzione precedente aveva legato le due cose alla STESSA macro. Con
 * un solo valore, GetWindowTextLengthA non puo' MAI superare il tetto del
 * controllo, quindi "len > FIN_LOG_TRONCA" non e' MAI vera: il troncamento
 * diventa codice morto. Il sintomo e' peggio di un pannello che si blocca:
 * registro_nuove marca le righe come consegnate non appena le copia nel
 * buffer, quindi le righe arrivate dopo la saturazione del controllo sono
 * PERSE per sempre, non solo in ritardo. Una prova che scrive meno del tetto
 * non lo scopre, perche' non esercita mai il ramo del troncamento.
 *
 * Il margine fra le due costanti deve bastare a contenere un intero blocco
 * di finestra_passo (il buffer di 8192 byte) fra un controllo della soglia
 * e il successivo: se il margine fosse troppo stretto lo stesso guasto si
 * ripresenterebbe in forma piu' rara. NON riunire queste due macro in una
 * sola, neppure per "semplificare": e' cio' che ha causato il guasto la
 * prima volta, e la seconda. */
#define FIN_LOG_TRONCA 400000
#define FIN_LOG_TETTO  (FIN_LOG_TRONCA + 65536)

/* UN COMMENTO IN MAIUSCOLO NON E' BASTATO: questa distinzione e' stata rotta
 * DUE VOLTE, la seconda da una correzione che credeva di sistemarla legando
 * le due macro allo stesso valore. Con FIN_LOG_TETTO == FIN_LOG_TRONCA (o
 * anche solo troppo vicine) GetWindowTextLengthA non puo' MAI superare
 * FIN_LOG_TRONCA prima che il controllo EDIT rifiuti in silenzio ogni
 * EM_REPLACESEL oltre il proprio tetto: "len > FIN_LOG_TRONCA" diventa codice
 * morto, e le righe arrivate dopo la saturazione sono PERSE per sempre (non
 * solo in ritardo), perche' registro_nuove le marca consegnate appena le
 * copia. Il margine (8192, il buffer di finestra_passo e' 8192 byte) deve
 * bastare a contenere un intero blocco di finestra_passo fra un controllo
 * della soglia e il successivo. Questa e' una asserzione a COMPILAZIONE,
 * apposta: un commento si legge solo se lo si cerca, un _Static_assert si
 * IMPONE al primo build. */
_Static_assert(FIN_LOG_TETTO > FIN_LOG_TRONCA + 8192,
               "FIN_LOG_TETTO deve stare almeno 8192 sopra FIN_LOG_TRONCA, o "
               "il troncamento del pannello diventa codice morto e le righe "
               "di seriale arrivate dopo la saturazione del controllo EDIT "
               "vengono perse in silenzio invece che solo ritardate");

/* La Config serve al trascinamento: rilascio_avvia ha bisogno della porta di adb.
 * Si tiene il puntatore e non una copia perche' nel guscio la configurazione vive
 * quanto il programma -- ed e' un requisito, non una comodita': il thread del
 * rilascio conserva questo puntatore e lo usa per minuti dopo che il wndproc ha
 * finito. */
static const Config *fin_config;
static HWND fin_hwnd;
static HWND fin_fasi;
static HWND fin_log;
static HWND fin_bottoni[9];
static char fin_testo_fasi[512];
static DWORD fin_ora_avvio;
static HFONT fin_font_ui;
static HFONT fin_font_log;
static UINT fin_dpi;

static const struct {
    int id;
    const char *etichetta;
} fin_elenco[] = {
    { ID_POWER,    "power"    },
    { ID_VOL_SU,   "volume +" },
    { ID_VOL_GIU,  "volume -" },
    { ID_HOME,     "home"     },
    { ID_BACK,     "back"     },
    { ID_RECENTI,  "recents"  },
    { ID_RUOTA,    "rotate"   },
    { ID_TASTI,    "keys"     },
    { ID_VARIANTE, "variant"  },
};

/* Il tempo trascorso non e' un ornamento: e' cio' che distingue "lento" da
 * "rotto". Un kernel che parte in 18 s e uno che parte in 90 danno la stessa
 * riga senza il numero. */
void finestra_fase(const char *nome, const char *esito)
{
    char aggiunta[128];
    DWORD ms = (DWORD)GetTickCount64() - fin_ora_avvio;

    snprintf(aggiunta, sizeof(aggiunta), "%s%s: %s (%lu,%lu s)",
             fin_testo_fasi[0] ? "    " : "", nome, esito,
             ms / 1000, (ms % 1000) / 100);
    strncat(fin_testo_fasi, aggiunta,
            sizeof(fin_testo_fasi) - strlen(fin_testo_fasi) - 1);
    if (fin_fasi) {
        SetWindowTextA(fin_fasi, fin_testo_fasi);
    }
}

void finestra_bottoni(bool attivi)
{
    size_t i;

    for (i = 0; i < sizeof(fin_bottoni) / sizeof(fin_bottoni[0]); i++) {
        if (fin_bottoni[i]) {
            EnableWindow(fin_bottoni[i], attivi ? TRUE : FALSE);
        }
    }
}

/* Scrive SOLO l'etichetta: lo stato acceso/spento e' una decisione di
 * main.c (vedi il commento su ID_HOTKEY_TASTI la'), questa funzione si
 * limita a renderla visibile, come finestra_fase fa per le fasi di avvio. */
void finestra_bottone_tasti(bool accesa)
{
    size_t i;

    for (i = 0; i < sizeof(fin_elenco) / sizeof(fin_elenco[0]); i++) {
        if (fin_elenco[i].id == ID_TASTI) {
            if (fin_bottoni[i]) {
                SetWindowTextA(fin_bottoni[i], accesa ? "tasti ON" : "tasti");
            }
            return;
        }
    }
}

void finestra_passo(void)
{
    char buf[8192];
    int n;

    n = registro_nuove(buf, sizeof(buf));
    while (n > 0) {
        int len = GetWindowTextLengthA(fin_log);

        /* Il controllo EDIT ha un limite pratico: oltre qualche centinaio di
         * migliaia di caratteri diventa lento a scorrere. Si tronca dalla testa
         * invece di rallentare, perche' cio' che interessa e' sempre in fondo.
         *
         * Si segna il taglio con una riga invece di farlo in silenzio: un
         * pannello che perde la propria testa senza dirlo sembra piu'
         * completo di quanto sia, e chi lo apre per capire un guasto merita
         * di sapere che l'inizio della sessione non c'e' piu', invece di
         * dedurlo da un buco nei tempi. Il costo e' una riga in piu' ogni
         * volta che si tronca, che nella pratica e' raro (serve una sessione
         * lunga per raggiungere la soglia). */
        if (len > FIN_LOG_TRONCA) {
            static const char marcatore[] =
                "[guscio] (le righe piu' vecchie sono state scartate)\r\n";

            SendMessageA(fin_log, EM_SETSEL, 0, 200000);
            SendMessageA(fin_log, EM_REPLACESEL, FALSE, (LPARAM)marcatore);
            len = GetWindowTextLengthA(fin_log);
        }
        SendMessageA(fin_log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageA(fin_log, EM_REPLACESEL, FALSE, (LPARAM)buf);
        SendMessageA(fin_log, EM_SCROLLCARET, 0, 0);
        n = registro_nuove(buf, sizeof(buf));
    }
}

HWND finestra_handle(void)
{
    return fin_hwnd;
}

/* IL CARATTERE VA SCALATO CON I CONTROLLI, o crescono loro e il testo no: il
 * sintomo sarebbe bottoni grandi con scritte minuscole in mezzo, che e' il
 * contrario dello scopo di questo lavoro.
 *
 * Altezza NEGATIVA e non positiva: in LOGFONT un'altezza negativa e' l'altezza
 * dei CARATTERI, quella positiva e' l'altezza della cella (caratteri piu'
 * interlinea). Mescolarle darebbe due dimensioni diverse per lo stesso numero, e
 * il testo uscirebbe piu' piccolo del richiesto senza che nulla lo dica.
 *
 * DUE caratteri e non uno: il pannello di log mostra righe di seriale, dove
 * l'allineamento delle colonne e' informazione (un timestamp, un livello, un
 * tag). Con un carattere proporzionale quelle colonne diventano irregolari e si
 * legge peggio, mentre per fasi e bottoni un proporzionale e' cio' che usa il
 * resto di Windows. Costa un HFONT in piu' e una DeleteObject. */
static HFONT fin_crea_font(UINT dpi, const wchar_t *nome)
{
    return CreateFontW(-dpi_scala(FIN_FONT_PX, dpi), 0, 0, 0, FW_NORMAL, FALSE,
                       FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, nome);
}

/* Dispone i controlli dentro un cliente di w x a pixel al DPI dato.
 *
 * UNICA funzione che conosce le costanti di posizione: la creazione, WM_SIZE e
 * WM_DPICHANGED la chiamano tutte e tre. E' anche cio' che rende WM_DPICHANGED
 * quasi gratuito -- il gestore non ha un layout suo, riusa questo.
 *
 * IDEMPOTENTE: chiamarla due volte con gli stessi argomenti non fa altro che
 * due giri di MoveWindow. Serve, perche' WM_DPICHANGED la chiama dopo un
 * SetWindowPos che a sua volta puo' aver mandato un WM_SIZE che l'ha gia'
 * chiamata. */
static void fin_disponi(int w, int a, UINT dpi)
{
    int m = dpi_scala(FIN_MARGINE, dpi);
    int bw = dpi_scala(FIN_BOT_W, dpi);
    int bh = dpi_scala(FIN_BOT_H, dpi);
    int passo = dpi_scala(FIN_BOT_PASSO, dpi);
    int gap = dpi_scala(FIN_BOT_GAP, dpi);
    int bot_y = dpi_scala(FIN_BOT_Y, dpi);
    int x = m;
    int riga = 0;
    int log_y;
    size_t i;

    /* SI ESCE SU UN CLIENTE DEGENERE, e non e' prudenza generica: MINIMIZZARE la
     * finestra manda un WM_SIZE con 0,0. Senza questa uscita ogni MoveWindow
     * riceverebbe larghezze negative (w - 2 * m con w = 0 fa -20), i controlli
     * verrebbero rimpiccioliti a nulla, e al ripristino resterebbero cosi'
     * finche' l'utente non ridimensiona a mano: il sintomo e' "ho minimizzato e
     * la finestra e' tornata vuota", che somiglia a un guasto del disegno e non
     * a una moltiplicazione per zero. */
    if (w <= 0 || a <= 0) {
        return;
    }

    /* Il carattere si rifa' solo quando il DPI cambia davvero: WM_SIZE arriva a
     * ogni pixel di trascinamento del bordo, e creare due HFONT per ognuno
     * consumerebbe handle GDI senza motivo. */
    if (dpi != fin_dpi || !fin_font_ui) {
        HFONT vecchio_ui = fin_font_ui;
        HFONT vecchio_log = fin_font_log;

        fin_font_ui = fin_crea_font(dpi, L"Segoe UI");
        fin_font_log = fin_crea_font(dpi, L"Consolas");
        if (fin_fasi) {
            SendMessageW(fin_fasi, WM_SETFONT, (WPARAM)fin_font_ui, TRUE);
        }
        for (i = 0; i < sizeof(fin_bottoni) / sizeof(fin_bottoni[0]); i++) {
            if (fin_bottoni[i]) {
                SendMessageW(fin_bottoni[i], WM_SETFONT,
                             (WPARAM)fin_font_ui, TRUE);
            }
        }
        if (fin_log) {
            SendMessageW(fin_log, WM_SETFONT, (WPARAM)fin_font_log, TRUE);
        }
        /* DOPO il WM_SETFONT del nuovo, non prima: distruggere un carattere
         * ancora in uso da un controllo lo lascerebbe disegnare con un handle
         * morto. E si distrugge, invece di lasciarlo andare: due HFONT per ogni
         * cambio di DPI sono una perdita di handle GDI, lenta ma vera. */
        if (vecchio_ui) {
            DeleteObject(vecchio_ui);
        }
        if (vecchio_log) {
            DeleteObject(vecchio_log);
        }
        fin_dpi = dpi;
    }

    if (fin_fasi) {
        MoveWindow(fin_fasi, m, dpi_scala(FIN_FASI_Y, dpi), w - 2 * m,
                   dpi_scala(FIN_FASI_H, dpi), TRUE);
    }
    /* I BOTTONI VANNO A CAPO, invece di uscire dalla vista. Nove bottoni a
     * passo 100 vogliono 910 pixel di riferimento, che a 200% diventano 1820: se
     * l'utente restringe la finestra sotto quella misura, con una riga sola gli
     * ultimi bottoni finiscono oltre il bordo destro e sono inarrivabili -- e
     * "power" e "home" non hanno nessun altro modo di essere premuti su un
     * dispositivo senza tasti fisici, quindi non e' un difetto estetico.
     *
     * Si va a capo solo se sulla riga c'e' gia' qualcosa (x > m): con una
     * finestra piu' stretta di UN bottone, andare a capo a ogni bottone darebbe
     * sette righe tutte fuori misura invece di una fila tagliata, che e' peggio.
     * In quel caso si accetta che sporgano: la finestra e' piu' stretta del suo
     * contenuto minimo, e non c'e' un layout giusto. */
    for (i = 0; i < sizeof(fin_bottoni) / sizeof(fin_bottoni[0]); i++) {
        if (!fin_bottoni[i]) {
            continue;
        }
        if (x > m && x + bw > w - m) {
            x = m;
            riga++;
        }
        MoveWindow(fin_bottoni[i], x, bot_y + riga * (bh + gap), bw, bh, TRUE);
        x += passo;
    }

    /* IL PANNELLO SEGUE I BOTTONI, e per questo la sua cima si calcola invece di
     * essere la costante 78 che era prima: con i bottoni su due righe una
     * costante li lascerebbe coperti dal pannello. Con una riga sola il calcolo
     * da' 40 + 28 + 10 = 78, cioe' esattamente il layout di oggi a 96 DPI. */
    log_y = bot_y + (riga + 1) * bh + riga * gap + m;
    if (fin_log) {
        int h = a - log_y - m;

        /* Un'altezza negativa o nulla non si passa a MoveWindow: con la finestra
         * schiacciata piu' dei bottoni il pannello non ci sta, e chiedere
         * un'altezza negativa e' un errore silenzioso. Zero e' l'unico valore
         * onesto: il pannello sparisce e ricompare appena c'e' posto. */
        MoveWindow(fin_log, m, log_y, w - 2 * m, h > 0 ? h : 0, TRUE);
    }
}

/* Un file trascinato sulla finestra: si riceve il gesto, si mostra il dialogo, e
 * niente altro. Le decisioni stanno in rilascio.c -- classificare, misurare,
 * scrivere il testo, eseguire -- perche' qui non si devono conoscere adb, push e
 * install: finestra.c non deve diventare il file che sa tutto.
 *
 * Il Piano sta sulla pila: ~8,6 KB, che nel wndproc ci stanno senza malloc. */
static void fin_rilascio(HDROP drop)
{
    Piano piano;
    char messaggio[1024];

    if (!rilascio_prepara(drop, &piano)) {
        /* Niente da fare, e la ragione e' gia' nel registro: qui non se ne
         * aggiunge una seconda che direbbe la stessa cosa peggio. */
        return;
    }
    if (rilascio_messaggio(messaggio, sizeof(messaggio), &piano) == 0) {
        return;
    }
    /* MB_DEFBUTTON2 mette il predefinito su Annulla: un Invio premuto per
     * distrazione non deve installare ne' copiare niente. */
    if (MessageBoxA(fin_hwnd, messaggio, "Habumi",
                    MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK) {
        registro_riga(REG_GUSCIO, "drop: cancelled");
        return;
    }
    /* Lo stato lo legge chi lo conosce: vm_stato() sta qui, non dentro
     * rilascio.c, che cosi' non dipende dalla macchina a stati e si prova senza
     * linkarla. */
    rilascio_avvia(fin_config, &piano, vm_stato());
}

/* Chi esegue i messaggi del guscio: la registra main.c subito dopo
 * finestra_apri. NULL fino ad allora, e in quella finestra non puo' arrivarne
 * nessuno -- li posta questo stesso wndproc, oppure un thread che main.c non
 * ha ancora avviato. */
static FinMsgGuscio fin_msg_guscio;

void finestra_messaggi_guscio(FinMsgGuscio f)
{
    fin_msg_guscio = f;
}

static LRESULT CALLBACK fin_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        /* Chiudere la finestra del guscio E' chiudere l'applicazione: non
         * esistono due modi di uscire con due significati. Lo spegnimento lo
         * decide main.c, che sa in quale stato siamo. */
        PostMessageA(h, WM_APP + 1, 0, 0);
        return 0;

    case WM_DESTROY:
        /* Ci si toglie dalla catena degli ascoltatori PRIMA di sparire: Windows
         * la ripulisce da se' quando la finestra muore, ma farlo qui e' l'unico
         * punto in cui e' visibile che quella registrazione esisteva. Se non
         * era stata fatta, la chiamata fallisce e non fa nulla. */
        RemoveClipboardFormatListener(h);
        PostQuitMessage(0);
        return 0;

    case WM_CLIPBOARDUPDATE:
        /* Gli appunti di Windows sono cambiati. QUI NON SI FA NIENTE DI LUNGO,
         * come per ogni altro ramo di questo wndproc: leggere gli appunti,
         * convertire e consegnare al server costa, e questo messaggio arriva a
         * OGNI copia di chiunque, non solo delle nostre. Si rimanda a main.c
         * esattamente come fanno i bottoni con WM_APP+2 -- e per la stessa
         * ragione in piu': finestra.c non deve conoscere il canale degli
         * appunti, o torna a essere il file che sa tutto. */
        PostMessageA(h, GUSCIO_MSG_APPUNTI_HOST, 0, 0);
        return 0;

    case WM_SIZE:
        fin_disponi(LOWORD(lp), HIWORD(lp), dpi_di_finestra(h));
        return 0;

    case WM_DROPFILES:
        fin_rilascio((HDROP)wp);
        /* DragFinish libera la memoria che Windows ha allocato per il
         * rilascio: senza, ogni trascinamento perde memoria. Sta qui e non
         * dentro fin_rilascio perche' la maniglia e' del messaggio, non della
         * funzione che la legge. */
        DragFinish((HDROP)wp);
        return 0;

    case WM_DPICHANGED: {
        /* LA FINESTRA E' PASSATA SU UNO SCHERMO CON SCALA DIVERSA.
         *
         * lParam porta il rettangolo SUGGERITO da Windows: si applica quello
         * invece di calcolarlo, perche' e' l'unico che tiene conto anche della
         * posizione relativa fra i due monitor -- una finestra a meta' fra due
         * schermi non ha una posizione "giusta" ricavabile dalla sola scala.
         *
         * wParam porta il DPI nuovo (identico nei due WORD, uno per asse) e si
         * usa quello invece di richiamare GetDpiForWindow: durante questo
         * messaggio e' la fonte autorevole.
         *
         * fin_disponi si chiama COMUNQUE dopo SetWindowPos, e non si conta sul
         * WM_SIZE che quello manda: se il rettangolo suggerito avesse la stessa
         * dimensione di adesso (due schermi di scala diversa possono darla),
         * SetWindowPos non manda nessun WM_SIZE e il layout resterebbe al DPI
         * vecchio. fin_disponi e' idempotente, quindi la chiamata in piu' nel
         * caso normale costa due giri di MoveWindow.
         *
         * LIMITE DICHIARATO: questo ramo NON e' stato verificato su un
         * passaggio vero fra due monitor -- questa macchina ha un solo schermo.
         * Vedi app/GUSCIO.md. */
        const RECT *r = (const RECT *)lp;
        RECT cliente;

        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left,
                     r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        GetClientRect(h, &cliente);
        fin_disponi(cliente.right, cliente.bottom, LOWORD(wp));
        registro_riga(REG_GUSCIO, "DPI changed to %u: geometry %ldx%ld "
                      "applied and layout redone",
                      (unsigned)LOWORD(wp), r->right - r->left,
                      r->bottom - r->top);
        return 0;
    }

    case WM_COMMAND:
        /* Si rimanda a main.c invece di premere i tasti qui: finestra.c non deve
         * conoscere adb, o diventerebbe il file che sa tutto.
         *
         * E si RIMANDA perche' WM_COMMAND arriva al wndproc e non alla coda che
         * GetMessage legge: un blocco "if (msg.message == WM_COMMAND)" nel ciclo
         * dei messaggi non scatterebbe mai. Il difetto sarebbe silenzioso --
         * bottoni che si premono e non fanno nulla. */
        PostMessageA(h, WM_APP + 2, wp, lp);
        return 0;

    case WM_TIMER:
        /* Il timer lo gestisce il ciclo dei messaggi in main.c, che sa lo stato
         * della VM. Qui si consuma per non lasciarlo cadere in DefWindowProc. */
        return 0;

    case WM_QUERYENDSESSION:
        /* L'UNICO momento in cui Windows concede tempo prima di uccidere tutto.
         * Si chiede la chiusura e si risponde TRUE: rifiutare bloccherebbe lo
         * spegnimento del computer, che nessuno vuole per un emulatore. Il tempo
         * concesso e' di alcuni secondi -- lo spegnimento pulito MISURATO (Task
         * 6, chiusura della finestra di Android) costa 4485 ms, quindi ci sta
         * di misura e non sempre: dipende da quanto Windows concede quel
         * giorno, e da questo non c'e' modo di saperlo in anticipo. */
        PostMessageA(h, WM_APP + 1, 0, 0);
        return TRUE;

    case WM_ENDSESSION:
        if (wp) {
            /* Windows sta chiudendo ADESSO: non c'e' piu' tempo per aspettare
             * lo spegnimento pulito, si fa il possibile e si esce. */
            registro_riga(REG_GUSCIO, "Windows is shutting down");
        }
        return 0;

    /* I MESSAGGI DEL GUSCIO SI CONSEGNANO DA QUI, non dal ciclo di main().
     * Vedi finestra_messaggi_guscio in guscio.h per il perche' esteso: in
     * breve, ogni modale (MessageBoxA, TrackPopupMenu) pompa con un ciclo
     * PROPRIO che di quel controllo non sa niente e li lascia cadere in
     * DefWindowProcA -- fra questi il WM_APP+1 di WM_QUERYENDSESSION, cioe' lo
     * spegnimento pulito della VM durante un arresto di Windows.
     *
     * Il numero nudo resta per i due che nascono in questo file (li posta
     * WM_CLOSE/WM_QUERYENDSESSION il primo, WM_COMMAND il secondo); gli altri
     * tre hanno un nome perche' arrivano da lontano. */
    case WM_APP + 1:                  /* chiusura chiesta */
    case WM_APP + 2:                  /* un bottone premuto */
    case GUSCIO_MSG_APPUNTI_HOST:
    case GUSCIO_MSG_APPUNTI_GUEST:
    case GUSCIO_MSG_VARIANTE_PRONTA:
        if (fin_msg_guscio) {
            fin_msg_guscio(msg, wp, lp);
        }
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

bool finestra_apri(const Config *c)
{
    WNDCLASSA wc = {0};
    size_t i;

    /* la configurazione serve davvero: il trascinamento di un
     * APK ha bisogno della porta di adb per installarlo. */
    fin_config = c;
    fin_ora_avvio = (DWORD)GetTickCount64();
    wc.lpfnWndProc = fin_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = FIN_CLASSE;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassA(&wc)) {
        return false;
    }

    fin_hwnd = CreateWindowA(FIN_CLASSE, "Habumi",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             FIN_LARGHEZZA, FIN_ALTEZZA, NULL, NULL,
                             wc.hInstance, NULL);
    if (!fin_hwnd) {
        return false;
    }

    /* Da qui la finestra accetta file trascinati. Va chiamata sulla finestra
     * PRINCIPALE: i controlli figli non ereditano l'accettazione, ed e' giusto
     * cosi' -- il rilascio vale per tutta la finestra, non per il pannello. */
    DragAcceptFiles(fin_hwnd, TRUE);

    /* E da qui la finestra riceve WM_CLIPBOARDUPDATE a ogni cambio degli appunti
     * di Windows, di chiunque sia la copia.
     *
     * AddClipboardFormatListener e non la vecchia catena di SetClipboardViewer:
     * quella incatena le finestre una all'altra e un solo osservatore che muore
     * male la spezza per tutti quelli che stanno sotto -- un difetto che non
     * sarebbe nostro ma che sembrerebbe nostro.
     *
     * Se fallisce si REGISTRA: senza questa riga il sintomo sarebbe "copio e nel
     * guest non arriva niente", identico a quello di un guest scollegato, e
     * nessuno saprebbe da quale dei due lati cercare. Non e' un motivo per non
     * aprire la finestra: il verso guest -> Windows continua a funzionare. */
    if (!AddClipboardFormatListener(fin_hwnd)) {
        registro_riga(REG_GUSCIO, "clipboard: the window could not start "
                      "listening for Windows clipboard changes "
                      "(%lu): what you copy here will not reach Android, "
                      "the other direction still works", GetLastError());
    }

    /* I controlli si creano a geometria ZERO e la prendono da fin_disponi poco
     * piu' sotto. Non e' pigrizia: al momento di CreateWindowA il DPI della
     * finestra non e' ancora noto, e scrivere qui delle coordinate vorrebbe dire
     * una seconda copia delle costanti di posizione -- che e' proprio la
     * duplicazione che ha gia' fatto divergere la larghezza del pannello
     * (FIN_LARGHEZZA - 40 alla creazione, w - 20 in WM_SIZE). */
    fin_fasi = CreateWindowA("STATIC", "avvio in corso...",
                             WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                             fin_hwnd, NULL, wc.hInstance, NULL);

    for (i = 0; i < sizeof(fin_elenco) / sizeof(fin_elenco[0]); i++) {
        fin_bottoni[i] = CreateWindowA("BUTTON", fin_elenco[i].etichetta,
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       0, 0, 0, 0, fin_hwnd,
                                       (HMENU)(INT_PTR)fin_elenco[i].id,
                                       wc.hInstance, NULL);
    }
    finestra_bottoni(false);

    fin_log = CreateWindowA("EDIT", "",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
                            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                            0, 0, 0, 0,
                            fin_hwnd, NULL, wc.hInstance, NULL);
    /* Va alzato SUBITO, prima che arrivi la prima riga di seriale: il default
     * di 30000 caratteri di un controllo EDIT si raggiunge in pochi secondi
     * di avvio (vedi la nota su FIN_LOG_TETTO/FIN_LOG_TRONCA sopra per la
     * misura). Si usa FIN_LOG_TETTO e non FIN_LOG_TRONCA apposta: il tetto
     * del controllo deve stare SOPRA la soglia di troncamento applicativo,
     * mai allo stesso valore, altrimenti il troncamento diventa
     * irraggiungibile (vedi il commento sopra la macro). */
    SendMessageA(fin_log, EM_SETLIMITTEXT, FIN_LOG_TETTO, 0);

    /* LA FINESTRA SI CREA ALLA DIMENSIONE DI RIFERIMENTO E POI SI SCALA. Al
     * momento di CreateWindowA non esiste ancora un HWND a cui chiedere il DPI,
     * quindi 760x520 sono pixel di riferimento: su questa macchina, a 200%,
     * sarebbero la META' della finestra che serve. Si scalano appena l'handle
     * esiste.
     *
     * Poi si dispone A MANO invece di contare sul WM_SIZE di SetWindowPos: a 96
     * DPI la dimensione non cambia, nessun WM_SIZE arriva, e i controlli
     * resterebbero a geometria zero -- una finestra vuota su ogni schermo non
     * scalato. Cioe' il guasto piu' facile da introdurre qui, e il piu' visibile.
     */
    {
        UINT dpi = dpi_di_finestra(fin_hwnd);
        RECT cliente;

        SetWindowPos(fin_hwnd, NULL, 0, 0, dpi_scala(FIN_LARGHEZZA, dpi),
                     dpi_scala(FIN_ALTEZZA, dpi),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        GetClientRect(fin_hwnd, &cliente);
        fin_disponi(cliente.right, cliente.bottom, dpi);
    }

    SetTimer(fin_hwnd, FIN_TIMER, 500, NULL);
    ShowWindow(fin_hwnd, SW_SHOW);
    return true;
}
