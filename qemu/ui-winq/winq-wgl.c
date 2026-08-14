/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-wgl.c -- il contesto host OpenGL DESKTOP, via WGL.
 *
 * PERCHE' ESISTE, col numero che lo giustifica. Misurato con
 * qemu/scripts/misura-prestazioni.sh, stesso kernel e stesso carico, cambiando
 * SOLO il backend di display:
 *
 *     winq (ANGLE/EGL, guest a ES 2.0)   mediana 34 ms, 13,6% scattosi, GPU 15 ms
 *     sdl  (WGL, guest a ES 3.1)         mediana  7 ms,  0,7% scattosi, GPU  4 ms
 *
 * Circa cinque volte. La catena e' breve: qemu_egl_init_dpy_win32 forza
 * DISPLAY_GL_MODE_ES su Windows (ui/egl-helpers.c:625-632), quindi ogni richiesta di
 * contesto DESKTOP da parte di virglrenderer fallisce con EGL_BAD_MATCH, virgl ripiega
 * sul proprio percorso, e il guest resta a ES 2.0 -- dove Mesa emula in software cio'
 * che il livello 3.x fa in hardware.
 *
 * Fallire e' la risposta giusta di ANGLE: restituire un contesto ES spacciandolo per
 * desktop farebbe usare a vrend funzioni assenti. Lo sbaglio e' chiedere ad ANGLE una
 * cosa che ANGLE non fa.
 *
 * COSA LA MACCHINA CONCEDE, misurato prima di scrivere questo file con
 * qemu/probe/sonda-wgl.c e non dedotto:
 *     VERSION  4.6 (Core Profile) Mesa 26.2.0-devel
 *     RENDERER D3D12 (Qualcomm(R) Adreno(TM) X1-85 GPU)
 *     4.6, 4.5, 4.3, 3.3, 3.2 core: tutti ottenuti
 *     contesto CONDIVISO 3.3 core: ottenuto
 *     WGL_ARB_create_context, _profile, pixel_format, EXT_swap_control: tutte presenti
 *
 * La condivisione non e' un dettaglio ma il requisito su cui tutto poggia:
 * virglrenderer crea un contesto per ogni contesto GL del guest, e le texture di
 * quei contesti devono essere visibili al nostro contesto di finestra per poterle
 * presentare. In SDL e' SDL_GL_SHARE_WITH_CURRENT_CONTEXT (ui/sdl2-gl.c:152); qui e'
 * il secondo argomento di wglCreateContextAttribsARB.
 *
 * PERCHE' NON E' IL DEFAULT. ANGLE e' il percorso che funziona oggi, verificato col
 * dito dell'utente. Si sceglie con WINQ_GL=wgl, e il default resta angle finche' l'A/B
 * non e' stato approvato: perdere un percorso funzionante per guadagnare prestazioni
 * sarebbe uno scambio cattivo, e tenerne due costa un ramo.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "ui/console.h"
#include "system/system.h"    /* display_opengl, che egl_init alzerebbe per noi */
#include <epoxy/gl.h>         /* glGetString e le sue costanti, via dispatch dinamico */
#include "winq.h"

#include <windows.h>

/* Dichiarate a mano e non prese da un header di estensioni: QEMU non porta
 * wglext.h, e le tre costanti che servono stanno in tre righe. */
typedef HGLRC (WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
typedef BOOL (WINAPI *PFN_wglSwapIntervalEXT)(int);
typedef const char * (WINAPI *PFN_wglGetExtensionsStringARB)(HDC);

#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
/* WGL_ARB_context_flush_control. NONE vale zero, ed e' un valore legittimo:
 * per questo l'elenco di attributi si chiude con una coppia in piu' e non con
 * il solo zero finale -- uno zero in posizione di VALORE non termina niente,
 * uno in posizione di CHIAVE si'. */
#define WGL_CONTEXT_RELEASE_BEHAVIOR_ARB       0x2097
#define WGL_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB  0x0000
#define WGL_CONTEXT_RELEASE_BEHAVIOR_FLUSH_ARB 0x2098

/* LE wgl* SI CARICANO A MANO DA opengl32.dll, e non si collegano.
 *
 * Il collegamento diretto fallisce: "undefined symbol: __declspec(dllimport)
 * wglCreateContext", perche' QEMU collega gdi32 e user32 -- da cui vengono GetDC,
 * ChoosePixelFormat e SwapBuffers -- ma non opengl32, e non ha ragione di farlo:
 * il suo percorso GL su Windows passa da ANGLE, che e' EGL.
 *
 * Aggiungere -lopengl32 ai flag di QEMU si potrebbe, e si e' scelto di no: vorrebbe
 * dire che qemu/scripts/innesta-winq.sh, che oggi e' una copia di file piu' due
 * righe di meson, dovrebbe anche modificare la configurazione del collegamento. Un
 * innesto che tocca meno cose e' un innesto che si riapplica su una versione nuova di
 * QEMU senza sorprese.
 *
 * Caricare a runtime ha anche un vantaggio proprio: se opengl32 non ci fosse -- o non
 * portasse WGL -- si scopre con un messaggio nostro invece di un eseguibile che non
 * parte. */
static HGLRC (WINAPI *p_wglCreateContext)(HDC);
static BOOL (WINAPI *p_wglMakeCurrent)(HDC, HGLRC);
static BOOL (WINAPI *p_wglDeleteContext)(HGLRC);
static PROC (WINAPI *p_wglGetProcAddress)(LPCSTR);
static HGLRC (WINAPI *p_wglGetCurrentContext)(void);

static bool winq_wgl_carica(void)
{
    HMODULE m;

    if (p_wglCreateContext) {
        return true;
    }
    m = LoadLibraryW(L"opengl32.dll");
    if (!m) {
        error_report("winq/wgl: opengl32.dll cannot be loaded (%lu). On Windows ARM64 "
                     "desktop GL comes from OpenGLOn12, which is part of the system: "
                     "if it is missing, use ANGLE by removing WINQ_GL=wgl.", GetLastError());
        return false;
    }

#define CARICA(nome)                                                          \
    do {                                                                      \
        p_##nome = (void *)GetProcAddress(m, #nome);                           \
        if (!p_##nome) {                                                      \
            error_report("winq/wgl: %s missing from opengl32.dll", #nome);       \
            return false;                                                     \
        }                                                                     \
    } while (0)
    CARICA(wglCreateContext);
    CARICA(wglMakeCurrent);
    CARICA(wglDeleteContext);
    CARICA(wglGetProcAddress);
    CARICA(wglGetCurrentContext);
#undef CARICA
    return true;
}

static HDC winq_hdc;
static HGLRC winq_wctx;                  /* il contesto con cui presentiamo */
static PFN_wglCreateContextAttribsARB winq_crea_attribs;

/* WGL E' IL DEFAULT, e ANGLE il ripiego. Si torna ad ANGLE con WINQ_GL=angle.
 *
 * PERCHE' IL DEFAULT E' CAMBIATO, con le misure che l'hanno deciso:
 *     scorrimento del launcher   ANGLE 34 ms di mediana, 13,6% scattosi
 *                                WGL    5 ms,             0,69%
 *     glmark2 2023.01            ANGLE 318, vince 3 prove su 29
 *                                WGL   535, vince 26 su 29        (+68%)
 * piu' la verifica dell'utente su immagine e tocco, che nessun numero da'.
 *
 * PERCHE' ORA IL RIPIEGO E' AUTOMATICO, mentre prima lo rifiutavo di proposito.
 * Quando WGL si chiedeva a mano, fallire in silenzio sarebbe stato peggio di fallire:
 * si sarebbero misurate prestazioni da ES 2.0 credendo di misurare desktop GL. Da
 * default la scelta si rovescia -- una macchina senza OpenGLOn12, o con un driver che
 * non concede un profilo core, non deve NON PARTIRE per una scelta di prestazioni. Si
 * ripiega, e si dice perche'.
 *
 * Lo stato ha tre valori e non due, e servono tutti: -1 non deciso, 1 attivo, 0 spento.
 * Zero significa ANGLE, sia per scelta dell'utente sia perche' WGL ha fallito, e una
 * volta a zero non si torna indietro -- a meta' vita del processo il formato pixel
 * dell'HDC e' stato fissato, e ripensarci non e' possibile. */
static int winq_wgl_stato = -1;

bool winq_wgl_attivo(void)
{
    const char *v;

    if (winq_wgl_stato < 0) {
        v = getenv("WINQ_GL");
        winq_wgl_stato = (v && !strcmp(v, "angle")) ? 0 : 1;
        if (!winq_wgl_stato) {
            info_report("winq: WINQ_GL=angle, using ANGLE instead of the desktop "
                        "desktop. The guest will stay on OpenGL ES 2.0.");
        }
    }
    return winq_wgl_stato == 1;
}

/* Da chiamare quando WGL ha fallito: da qui in poi si e' su ANGLE, e ogni bivio deve
 * saperlo. Separata da winq_wgl_attivo perche' quella e' una domanda e questa una
 * decisione, e confonderle e' il modo in cui nasce uno stato che si contraddice. */
void winq_wgl_rinuncia(void)
{
    winq_wgl_stato = 0;
}

/* Imposta il formato pixel sull'HDC della finestra.
 *
 * VA FATTO UNA VOLTA E PER SEMPRE, e non alla creazione di ogni contesto: Windows
 * rifiuta un secondo SetPixelFormat sullo stesso HDC. Da qui la separazione fra
 * questa funzione e la creazione dei contesti.
 *
 * Presuppone CS_OWNDC sulla classe della finestra: senza, ogni GetDC puo' restituire
 * un HDC diverso, e il formato impostato su uno non vale sull'altro -- con il
 * risultato che wglCreateContext fallisce su un HDC apparentemente identico. */
static bool winq_wgl_formato(HWND hwnd)
{
    PIXELFORMATDESCRIPTOR pfd = {0};
    int pf;

    winq_hdc = GetDC(hwnd);
    if (!winq_hdc) {
        error_report("winq/wgl: GetDC failed (%lu)", GetLastError());
        return false;
    }

    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    /* Profondita' e stencil: il percorso 2D di QEMU non ne usa, ma i contesti che
     * virglrenderer chiede per il guest si' -- Android compone con il test di
     * profondita' attivo. Chiederli qui costa niente e non chiederli darebbe un
     * guasto dentro il guest, lontano da questa riga. */
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;

    pf = ChoosePixelFormat(winq_hdc, &pfd);
    if (!pf) {
        error_report("winq/wgl: ChoosePixelFormat finds no format with "
                     "OpenGL (%lu)", GetLastError());
        return false;
    }
    if (!SetPixelFormat(winq_hdc, pf, &pfd)) {
        error_report("winq/wgl: SetPixelFormat failed (%lu)", GetLastError());
        return false;
    }
    return true;
}

/* Crea un contesto core, provando dalla versione piu' alta in giu'.
 * `condiviso` e' il contesto con cui condividere gli oggetti, o NULL. */
/* WGL_ARB_context_flush_control: SI CHIEDE, non si impone da fuori.
 *
 * COSA CAMBIA RISPETTO A PRIMA. Fino al la stessa cosa si otteneva
 * con MESA_WGL_NO_FLUSH_WAIT=1, cioe' una variabile d'ambiente che cambiava la
 * semantica di GL sotto i piedi di chiunque girasse in questo processo. Adesso
 * lo chiede chi crea il contesto, con l'attributo che la specifica prevede, e
 * vale SOLO per i contesti che lo chiedono. La variabile resta in Mesa come
 * scavalco globale per l'A/B, non come via normale.
 *
 * PERCHE' SI VERIFICA L'ESTENSIONE. Un attributo ignoto fa fallire
 * wglCreateContextAttribsARB con ERROR_INVALID_PARAMETER -- giustamente: e'
 * quello che la nostra implementazione fa con un valore fuori dai due
 * ammessi. Su una opengl32.dll che non la espone (quella di sistema, o una
 * Mesa senza le nostre patch) chiederla senza guardare significherebbe non
 * avere piu' nessun contesto, cioe' niente immagine. Si guarda, e se non c'e'
 * si tace: il comportamento e' quello di sempre.
 *
 * WINQ_GL_FLUSH_WAIT=yes riporta l'attesa senza ricompilare, ed e' la chiave
 * gl_flush_wait di config.txt che arriva fin qui (vedi app/guscio/vm.c). */
static bool winq_wgl_attesa_richiesta(void)
{
    static int acceso = -1;

    if (acceso < 0) {
        const char *v = getenv("WINQ_GL_FLUSH_WAIT");

        acceso = (v && *v == 'y') ? 1 : 0;
    }
    return acceso != 0;
}

static bool winq_wgl_ha_flush_control(void)
{
    static int presente = -1;

    if (presente < 0) {
        PFN_wglGetExtensionsStringARB p_estensioni =
            (PFN_wglGetExtensionsStringARB)(void *)
            p_wglGetProcAddress("wglGetExtensionsStringARB");
        const char *s = p_estensioni ? p_estensioni(winq_hdc) : NULL;

        presente = (s && strstr(s, "WGL_ARB_context_flush_control")) ? 1 : 0;
        info_report("winq/wgl: WGL_ARB_context_flush_control %s",
                    presente ? "c'e', i contesti chiederanno "
                               "CONTEXT_RELEASE_BEHAVIOR_NONE"
                             : "assente, si resta al flush con attesa");
    }
    return presente != 0;
}

static HGLRC winq_wgl_crea(HGLRC condiviso, int major, int minor)
{
    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, major,
        WGL_CONTEXT_MINOR_VERSION_ARB, minor,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0, 0,   /* posto per l'attributo del flush control */
        0
    };

    if (!winq_wgl_attesa_richiesta() && winq_wgl_ha_flush_control()) {
        attribs[6] = WGL_CONTEXT_RELEASE_BEHAVIOR_ARB;
        attribs[7] = WGL_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB;
    }
    return winq_crea_attribs(winq_hdc, condiviso, attribs);
}

/* Quello che si puo' fare PRIMA che la finestra esista, cioe' quasi nulla.
 *
 * IL DIFETTO CHE HA IMPOSTO QUESTA DIVISIONE, e va scritto perche' non ha prodotto
 * nessun errore. winq_wgl_init era chiamata da winq_present_early_init, che gira prima
 * di winq_create_window: winq.hwnd era ancora NULL, e GetDC(NULL) NON fallisce --
 * restituisce il DC dell'INTERO SCHERMO. Su quel DC ChoosePixelFormat, SetPixelFormat
 * e wglCreateContextAttribsARB riuscivano tutti, il contesto nasceva 4.6 core su
 * D3D12, il blit non dava errori e i framebuffer erano COMPLETE.
 *
 * Solo che ogni SwapBuffers presentava allo schermo invece che alla nostra finestra, e
 * la finestra mostrava il pennello di sfondo della propria classe: IMMAGINE TUTTA
 * BIANCA, con ogni contatore che diceva successo. Riferito dall'utente come "l'immagine
 * e' tutta bianca", che e' l'unico sintomo che esisteva.
 *
 * La lezione, la stessa di altri tre difetti in questo progetto: una API che accetta
 * NULL e fa qualcosa di sensato con NULL e' piu' pericolosa di una che fallisce.
 *
 * display_opengl va alzato QUI e non piu' tardi: e' l'interruttore con cui QEMU decide
 * se il percorso GL esiste, e lo legge fra early_init e la creazione della console.
 * Nel percorso EGL lo alza egl_init (ui/egl-helpers.c:733), che sta nello stesso
 * punto. */
bool winq_wgl_early_init(void)
{
    if (!winq_wgl_carica()) {
        return false;
    }
    display_opengl = 1;
    return true;
}

bool winq_wgl_init(HWND hwnd)
{
    HGLRC legacy;
    PFN_wglSwapIntervalEXT intervallo;
    const unsigned char *v;
    static const int versioni[][2] = { {4, 6}, {4, 5}, {4, 3}, {3, 3}, {3, 2} };
    size_t i;

    /* La finestra deve ESISTERE. Senza questo controllo GetDC(NULL) darebbe il DC
     * dello schermo e si tornerebbe al difetto descritto sopra, che non si annuncia. */
    if (!hwnd) {
        error_report("winq/wgl: winq_wgl_init called without a window. The context "
                     "must be created after winq_create_window, not in early_init.");
        return false;
    }
    if (!winq_wgl_carica() || !winq_wgl_formato(hwnd)) {
        return false;
    }

    /* IL GIRO OBBLIGATO IN DUE PASSI. wglGetProcAddress funziona solo con un
     * contesto GIA' corrente, quindi per ottenere il puntatore a
     * wglCreateContextAttribsARB -- che e' l'unica via per chiedere un profilo core
     * -- serve prima un contesto legacy usa-e-getta. Non e' un giro nostro: e' come
     * WGL e' fatto, e ogni programma che vuole GL moderno su Windows lo ripete. */
    legacy = p_wglCreateContext(winq_hdc);
    if (!legacy) {
        error_report("winq/wgl: wglCreateContext failed (%lu): no OpenGL "
                     "on this HDC", GetLastError());
        return false;
    }
    if (!p_wglMakeCurrent(winq_hdc, legacy)) {
        error_report("winq/wgl: wglMakeCurrent on the legacy context failed (%lu)",
                     GetLastError());
        p_wglDeleteContext(legacy);
        return false;
    }

    winq_crea_attribs = (PFN_wglCreateContextAttribsARB)(void *)
        p_wglGetProcAddress("wglCreateContextAttribsARB");
    if (!winq_crea_attribs) {
        error_report("winq/wgl: wglCreateContextAttribsARB is missing. Without it "
                     "a CORE profile cannot be requested, and that is the only thing "
                     "that vrend accepts as a desktop context. Try again without "
                     "WINQ_GL=wgl to go back to ANGLE.");
        p_wglMakeCurrent(NULL, NULL);
        p_wglDeleteContext(legacy);
        return false;
    }

    for (i = 0; i < ARRAY_SIZE(versioni) && !winq_wctx; i++) {
        winq_wctx = winq_wgl_crea(NULL, versioni[i][0], versioni[i][1]);
    }

    /* Il contesto legacy si distrugge solo DOPO aver smesso di usarlo come
     * corrente: wglDeleteContext su un contesto corrente non fa nulla e ritorna
     * errore, e il contesto resterebbe vivo per sempre. */
    p_wglMakeCurrent(NULL, NULL);
    p_wglDeleteContext(legacy);

    if (!winq_wctx) {
        error_report("winq/wgl: no core context obtained, from 4.6 down to 3.2. "
                     "Try again without WINQ_GL=wgl.");
        return false;
    }
    if (!p_wglMakeCurrent(winq_hdc, winq_wctx)) {
        error_report("winq/wgl: wglMakeCurrent on the core context failed (%lu)",
                     GetLastError());
        return false;
    }

    /* Vsync spento. Con lo swap sincronizzato, SwapBuffers BLOCCA il thread
     * principale di QEMU fino al ritracciamento -- e su quel thread girano anche la
     * pompa dei messaggi e il ciclo principale, quindi bloccarlo li' vorrebbe dire
     * fermare l'input e i timer. La cadenza la da' gia' il battito di winq-window.c. */
    intervallo = (PFN_wglSwapIntervalEXT)(void *)
        p_wglGetProcAddress("wglSwapIntervalEXT");
    if (intervallo) {
        intervallo(0);
    }

    v = glGetString(GL_VERSION);
    info_report("winq/wgl: desktop context ready -- GL_VENDOR=%s "
                "GL_RENDERER=%s GL_VERSION=%s",
                (const char *)glGetString(GL_VENDOR),
                (const char *)glGetString(GL_RENDERER),
                v ? (const char *)v : "(null)");
    return true;
}

/* --- le quattro DisplayGLCtxOps, nella variante WGL ---------------------------
 *
 * Stesse responsabilita' delle qemu_egl_* che sostituiscono, e QEMUGLContext e' un
 * void* quindi ci sta un HGLRC. */

QEMUGLContext winq_wgl_ctx_create(DisplayGLCtx *dgc, QEMUGLParams *params)
{
    HGLRC ctx;

    /* winq_wctx come contesto condiviso: e' questa riga che mette le texture del
     * guest nella portata del contesto con cui presentiamo. Senza, la texture di
     * scanout esisterebbe in un contesto che il nostro non vede, e il blit
     * disegnerebbe il nulla senza errori. */
    ctx = winq_wgl_crea(winq_wctx, params->major_ver, params->minor_ver);
    if (!ctx) {
        /* Non si ripiega a una versione piu' bassa di quella chiesta: virglrenderer
         * chiede scendendo da se' (4.6, 4.5, ...), e dargli un contesto piu' basso
         * di quello richiesto significherebbe fargli credere di avere funzioni che
         * non ha. Fallire e' la risposta corretta, come lo era per ANGLE. */
        warn_report_once("winq/wgl: core context %d.%d refused (%lu); "
                         "virglrenderer will try a lower version",
                         params->major_ver, params->minor_ver, GetLastError());
    }
    return ctx;
}

void winq_wgl_ctx_destroy(DisplayGLCtx *dgc, QEMUGLContext ctx)
{
    if (ctx) {
        /* SI STACCA PRIMA DI CANCELLARE, e su WGL non e' pedanteria come lo
         * era su EGL.
         *
         * LA DIFFERENZA FRA LE DUE API. eglDestroyContext su un contesto
         * corrente RINVIA la distruzione: il thread continua ad avere un
         * contesto valido, e il codice chiamante -- che non sa di aver
         * distrutto cio' su cui sta lavorando -- se la cava. wglDeleteContext
         * cancella subito e lascia il thread SENZA contesto corrente. E'
         * l'unica riga di questo file che si comporta diversamente dalla
         * qemu_egl_* che sostituisce, ed e' quella che conta, perche'
         * vrend_destroy_context() (vrend_renderer.c) fa proprio cosi': rende
         * corrente il contesto che sta per distruggere, lo distrugge, e
         * continua a fare pulizia con glDelete*.
         *
         * COSA CAMBIA. Staccare non restituisce un contesto a chi continua a
         * chiamare GL -- non e' quello lo scopo, e mettere qui winq_wctx al
         * suo posto sarebbe peggio del male: le chiamate finirebbero nel
         * contesto della presentazione, riuscirebbero, e scriverebbero nel
         * posto sbagliato in silenzio. Lo scopo e' che lo stato sia ESPLICITO:
         * dopo questa riga nessuno e' corrente, wglGetCurrentContext lo dice, e
         * la prossima commutazione fa una make-current vera. La contabilita'
         * che deve chiederla e' la riga gemella in vrend_destroy_context, che
         * azzera current_hw_ctx: senza quella, la scorciatoia salterebbe la
         * make-current fidandosi di un puntatore liberato.
         *
         * MISURATO : 86 chiamate GL "without a rendering context"
         * per sessione prima delle due correzioni, con la make-current che non
         * fallisce mai (falliti=0 su 237 campioni al secondo) perche' nessuno
         * la chiamava. */
        if (p_wglGetCurrentContext() == (HGLRC)ctx) {
            static bool detto;

            if (!detto) {
                detto = true;
                info_report("winq/wgl: deleting the CURRENT context -- detached "
                            "first, so that the state is explicit and the next "
                            "switch asks for a real make-current");
            }
            p_wglMakeCurrent(winq_hdc, NULL);
        }
        p_wglDeleteContext((HGLRC)ctx);
    }
}

/* UNA MAKE-CURRENT FALLITA NON DEVE ESSERE MUTA, e finora lo era.
 *
 * IL DIFETTO CHE HA FATTO SCRIVERE QUESTO. Misurato con
 * MESA_DEBUG=1 acceso: 54 chiamate GL in una sessione escono con
 * "GL User Error: glDeleteBuffers/glGenTextures/glTexStorage2D/glFenceSync
 * called without a rendering context", la prima a 21,8 s dall'avvio. La
 * glFenceSync senza contesto torna NULL, virglrenderer lo riporta come
 * "Failed to create fence sync object", e il comando che sta passando in quel
 * momento paga per tutti: vrend_check_no_error() (vrend_decode.c:2141) trova
 * l'errore GL pendente e trasforma un TRANSFER3D riuscito in EINVAL, il
 * contesto viene marcato in errore, e virglrenderer smette di eseguire i suoi
 * buffer. A schermo: le icone del launcher non si disegnano piu', o
 * l'applicazione che usava quel contesto esce. Un contesto per volta, e per
 * questo sparisce PARTE della grafica e non tutta.
 *
 * PERCHE' NESSUNO SE NE ACCORGEVA. Tre ritorni buttati in fila: questa
 * funzione restituiva -1 senza dire niente; virgl_make_context_current
 * (hw/display/virtio-gpu-virgl.c) lo passa al chiamante senza una parola; e
 * virglrenderer fa assert su quel ritorno solo con le callback a versione >= 4
 * -- su WGL la versione resta 3 di proposito, e in release b_ndebug spegne
 * comunque gli assert. Il fallimento era invisibile per costruzione.
 *
 * COSA DEVE DISTINGUERE. Due cause danno lo stesso sintomo e vogliono cure
 * diverse: (a) wglMakeCurrent FALLISCE -- mi aspetto 170, ERROR_BUSY, cioe' il
 * contesto e' corrente su un altro thread, e la causa sarebbe l'unico
 * winq_hdc (:120) condiviso fra il thread principale e quello di sync; (b)
 * nessuno la chiama affatto su quel percorso, e allora manca una make-current
 * in un ramo di distruzione risorse. Il conteggio "ok" e i thread che chiamano
 * separano i due casi: con Mesa che lamenta chiamate senza contesto e
 * falliti=0, la risposta e' (b).
 *
 * IL PRIMO FALLIMENTO SI STAMPA SEMPRE, senza interruttore: e' un difetto
 * vero, non una curiosita' da misura, e oggi non lascia traccia. Il resto
 * riepiloga AL SECONDO come tutte le altre diagnostiche di questo albero --
 * qui la funzione passa 2663 volte al secondo sotto carico, e una error_report
 * per chiamata costa 356 us misurati, piu' di un intero giro del battito.
 *
 * I CONTATORI NON SONO ATOMICI, ed e' deliberato: li scrivono piu' thread, ma
 * un incremento perso in una diagnostica non cambia la conclusione (un ordine
 * di grandezza, non un'unita'), mentre un lock su un percorso da 2663
 * chiamate al secondo cambierebbe la cosa misurata. */
#define WINQ_MC_THREAD_MAX 4
static unsigned long long winq_mc_ultimo_ms;
static unsigned winq_mc_ok, winq_mc_falliti, winq_mc_stacchi;
static unsigned long winq_mc_ultimo_errore, winq_mc_thread_fallito;
static unsigned long winq_mc_thread_id[WINQ_MC_THREAD_MAX];
static unsigned winq_mc_thread_conta[WINQ_MC_THREAD_MAX];

static bool winq_mc_diag(void)
{
    static int acceso = -1;

    if (acceso < 0) {
        const char *v = getenv("WINQ_MAKECURRENT_DIAG");

        acceso = (v && *v == '1') ? 1 : 0;
    }
    return acceso != 0;
}

/* Chi chiama, non quante volte in totale: quattro caselle bastano perche' i
 * thread che toccano GL in questo processo sono il principale, quello della
 * finestra e quello di sync di virglrenderer. Il quinto in poi non si conta, e
 * si preferisce non contarlo che allargare la tabella a indovinare. */
static void winq_mc_thread_segna(unsigned long id)
{
    int i;

    for (i = 0; i < WINQ_MC_THREAD_MAX; i++) {
        if (winq_mc_thread_id[i] == id) {
            winq_mc_thread_conta[i]++;
            return;
        }
        if (winq_mc_thread_id[i] == 0) {
            winq_mc_thread_id[i] = id;
            winq_mc_thread_conta[i] = 1;
            return;
        }
    }
}

int winq_wgl_make_current(DisplayGLCtx *dgc, QEMUGLContext ctx)
{
    static bool primo_fallimento = true;
    BOOL riuscita = p_wglMakeCurrent(winq_hdc, (HGLRC)ctx);
    unsigned long long adesso;

    /* p_wglMakeCurrent(hdc, NULL) e' il modo di STACCARE, e va distinto dal
     * fallimento: QEMU passa NULL per rilasciare il contesto. */
    if (!riuscita) {
        winq_mc_ultimo_errore = GetLastError();
        winq_mc_thread_fallito = GetCurrentThreadId();
        winq_mc_falliti++;
        if (primo_fallimento) {
            primo_fallimento = false;
            error_report("winq/wgl: wglMakeCurrent(ctx=%p) FAILED (%lu) on thread "
                         "%lu -- from here on the GL of this thread runs with no "
                         "context, and the first command that pays for it will be "
                         "an innocent one",
                         (void *)ctx, winq_mc_ultimo_errore,
                         winq_mc_thread_fallito);
        }
    } else if (!ctx) {
        winq_mc_stacchi++;
    } else {
        winq_mc_ok++;
    }
    if (winq_mc_diag()) {
        winq_mc_thread_segna((unsigned long)GetCurrentThreadId());
    }

    adesso = GetTickCount64();
    if ((winq_mc_falliti || winq_mc_diag()) &&
        adesso - winq_mc_ultimo_ms >= 1000) {
        winq_mc_ultimo_ms = adesso;
        error_report("winq-mc/s: ok=%u falliti=%u stacchi=%u ultimo_errore=%lu "
                     "thread_fallito=%lu thread=[%lu:%u %lu:%u %lu:%u %lu:%u]",
                     winq_mc_ok, winq_mc_falliti, winq_mc_stacchi,
                     winq_mc_ultimo_errore, winq_mc_thread_fallito,
                     winq_mc_thread_id[0], winq_mc_thread_conta[0],
                     winq_mc_thread_id[1], winq_mc_thread_conta[1],
                     winq_mc_thread_id[2], winq_mc_thread_conta[2],
                     winq_mc_thread_id[3], winq_mc_thread_conta[3]);
        winq_mc_ok = 0;
        winq_mc_falliti = 0;
        winq_mc_stacchi = 0;
        winq_mc_thread_conta[0] = 0;
        winq_mc_thread_conta[1] = 0;
        winq_mc_thread_conta[2] = 0;
        winq_mc_thread_conta[3] = 0;
    }
    return riuscita ? 0 : -1;
}

QEMUGLContext winq_wgl_get_current(DisplayGLCtx *dgc)
{
    return p_wglGetCurrentContext();
}

/* --- presentazione e dimensione ---------------------------------------------- */

bool winq_wgl_corrente(void)
{
    if (!winq_wctx) {
        return false;
    }
    if (p_wglGetCurrentContext() == winq_wctx) {
        return true;
    }
    return p_wglMakeCurrent(winq_hdc, winq_wctx) != 0;
}

void winq_wgl_swap(void)
{
    SwapBuffers(winq_hdc);
}

/* La dimensione VERA della superficie di disegno.
 *
 * Con EGL questa informazione si chiedeva a eglQuerySurface, perche' ANGLE ricrea la
 * catena di scambio dentro eglSwapBuffers -- cioe' DOPO che abbiamo disegnato -- e la
 * finestra e la superficie potevano non concordare per un frame.
 *
 * Con WGL il problema non esiste nella stessa forma: il framebuffer di default e'
 * l'area cliente della finestra, e GetClientRect la da' senza intermediari. Questa e'
 * la ragione per cui il chiavistello del riarmo (winq_riarmi) non serve su questo
 * percorso -- ma resta armato comunque, perche' costa un confronto e disarmarlo
 * sarebbe una scommessa non misurata. */
bool winq_wgl_dimensione(int *w, int *h)
{
    RECT r;

    if (!GetClientRect(WindowFromDC(winq_hdc), &r)) {
        return false;
    }
    *w = r.right - r.left;
    *h = r.bottom - r.top;
    return *w > 0 && *h > 0;
}
