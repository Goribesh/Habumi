/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-present.c
 * Le quattro operazioni di contesto GL e la presentazione della texture del
 * guest sulla nostra finestra Win32.
 *
 * DUE responsabilita', non una, e la prima non era prevista:
 *
 *  1. le DisplayGLCtxOps (include/ui/console.h:279-288). virtio-gpu-gl-pci non
 *     si accontenta del flag globale display_opengl: quando virglrenderer parte
 *     chiede a QEMU un contesto GL vero con dpy_gl_ctx_create
 *     (hw/display/virtio-gpu-virgl.c:1315-1327 -> ui/console.c:981-985), che fa
 *     assert(con->gl). Senza queste quattro funzioni QEMU muore con
 *     "assertion failed: (con->gl)" a poche righe di avvio del guest, misurato
 * tre volte su tre in quella verifica. con->gl si popola solo con
 *     qemu_console_set_display_gl_ctx(), che winq_init chiama passando
 *     winq.dgc con questo ops.
 *
 *  2. il disegno della texture di scanout sulla finestra.
 *
 * Il contesto e' condiviso, e non per fortuna: ogni contesto che virglrenderer
 * chiede passa da qui e viene creato con qemu_egl_create_context(...,
 * qemu_egl_rn_ctx) (ui/egl-context.c:5-26), cioe' nello share group di
 * qemu_egl_rn_ctx; il contesto con cui presentiamo e' creato allo stesso modo,
 * nello stesso share group. Gli share group di EGL/GL sono insiemi: due
 * contesti creati entrambi condividendo con un terzo condividono anche fra
 * loro, quindi l'id di texture che arriva in dpy_gl_scanout_texture e' valido
 * nel nostro contesto e non serve nessuna copia. E' lo stesso schema di
 * ui/egl-headless.c:42-46 e ui/dbus.c:50. La prova a runtime, non solo sulla
 * carta, e' stampata una volta alla prima texture: vedi winq_prova_texture().
 *
 * Non si scrive EGL a mano dove QEMU ha gia' l'involucro: egl_init,
 * qemu_egl_init_surface_x11, qemu_egl_create_context/destroy/make_current,
 * egl_fb_*, qemu_gl_init_shader, surface_gl_*. L'elenco con file:riga sta nei
 * commenti alle singole chiamate.
 *
 * Niente thread: si disegna sul thread principale, dentro le callback che QEMU
 * chiama, come fa ui/sdl2-gl.c. Un thread di presentazione dovrebbe rendere
 * corrente lo stesso contesto altrove, ed e' proprio il rischio che la spec
 * chiedeva di evitare.
 *
 * Niente input qui: questo file non sa nulla di WM_*. Sa solo di GL.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "ui/egl-helpers.h"
#include "ui/egl-context.h"
#include "ui/shader.h"
#include "winq.h"

/* Stato della presentazione: vive qui, non in WinqState, perche' nessun altro
 * modulo deve poterlo toccare. winq-window.c non conosce EGL. */
static EGLSurface winq_esurface;
static EGLContext winq_ectx;
static QemuGLShader *winq_gls;
static egl_fb winq_guest_fb = EGL_FB_INIT;   /* la texture del guest, come FBO */
static egl_fb winq_win_fb = EGL_FB_INIT;     /* il framebuffer della finestra */
static DisplaySurface *winq_ds;              /* percorso 2D, quando non c'e' scanout */
static bool winq_scanout_mode;
static bool winq_y0_top;
static int winq_updates;                     /* aggiornamenti 2D non ancora mostrati */
static uint64_t winq_frames;                 /* frame presentati, per la diagnosi */

/* ------------------------------------------------------------------ */

/* La modalita' chiesta sulla riga di comando, ricordata perche' il ripiego su ANGLE
 * puo' avvenire in winq_present_init -- dopo che early_init e' tornata -- e a quel
 * punto egl_init la pretende ancora. */
static DisplayGLMode winq_mode;

bool winq_present_early_init(DisplayGLMode mode)
{
    Error *err = NULL;

    winq_mode = mode;

    /* egl_init (ui/egl-helpers.c:702-735) su WIN32 fa esattamente cio' che
     * serve, e lo fa gia': qemu_egl_init_dpy_win32(EGL_DEFAULT_DISPLAY, mode)
     * -- che forza la modalita' ES perche' e' quella che ANGLE sa fare
     * (ui/egl-helpers.c:625-629) -- poi qemu_egl_init_ctx() per il contesto
     * radice qemu_egl_rn_ctx, e alza display_opengl da se'. Stesso ingresso di
     * ui/egl-headless.c:214 e ui/dbus.c:515: non si ricopia a mano.
     * Il primo argomento (rendernode) e' ignorato su Windows. */
    /* IL BIVIO. Sul percorso desktop non si inizializza EGL affatto: le due strade non
     * convivono sullo stesso HDC, perche' il formato pixel si imposta una volta e per
     * sempre.
     *
     * Qui si fa solo il caricamento di opengl32 e display_opengl: la finestra non
     * esiste ancora, e creare il contesto adesso e' esattamente il difetto
     * dell'immagine tutta bianca. Se anche solo il caricamento fallisce si rinuncia
     * subito e si cade su EGL, senza aspettare present_init. */
    if (winq_wgl_attivo()) {
        if (winq_wgl_early_init()) {
            return true;
        }
        error_printf("winq: falling back to ANGLE, and the guest will stay on "
                     "OpenGL ES 2.0.\n");
        winq_wgl_rinuncia();
    }

    if (!egl_init(NULL, mode, &err)) {
        error_reportf_err(err, "winq: EGL initialisation failed: ");
        error_printf("winq: the runtime ships ANGLE next to the executable: "
                     "check libEGL.dll and libGLESv2.dll in runtime/bin.\n"
                     "winq: alternatively use -display sdl.\n");
        return false;
    }
    return true;
}

bool winq_present_init(void)
{
    /* Con WGL non c'e' nulla da fare qui, e la ragione dice come sono divise le due
     * funzioni. winq_present_early_init crea il contesto host; questa crea il
     * contesto con cui PRESENTIAMO piu' la superficie EGL su cui presentare. Nel
     * percorso WGL quei due oggetti non esistono come cose separate: il contesto core
     * creato da winq_wgl_init e' gia' quello di presentazione, ed e' gia' corrente, e
     * la superficie e' l'area cliente della finestra -- WGL non ha un oggetto
     * superficie da creare.
     *
     * Trovato eseguendo, non leggendo: il primo tentativo aveva messo il bivio nella
     * sola early_init, e il contesto WGL nasceva correttamente (4.6 core, D3D12) per
     * poi vedere questa funzione chiedere ad ANGLE un contesto GLES 3.0 e fallire con
     * EGL_BAD_DISPLAY, perche' nessun display EGL era stato inizializzato. Il
     * messaggio d'errore nominava ANGLE in un percorso che ANGLE non usa.
     *
     * Ma NON tutto qui e' EGL, e il primo tentativo ha sbagliato proprio questo:
     * uscendo subito si saltava anche qemu_gl_init_shader, e winq_gls restava nullo.
     * Il guasto arrivava tre chiamate piu' tardi, come
     *     ERROR:../ui/console-gl.c:73:surface_gl_create_texture: assertion failed: (gls)
     * cioe' un'asserzione dentro QEMU che non nomina ne' WGL ne' gli shader. Lo
     * shader serve a QUALUNQUE contesto: e' il programma con cui si disegna la
     * texture, non un dettaglio di EGL.
     *
     * Gli shader di ui/shader/ dichiarano "#version 300 es", che e' GLSL ES 3.00, e
     * un contesto desktop CORE li accetta perche' ARB_ES3_compatibility e' core da
     * OpenGL 4.3 -- e il nostro e' 4.6. Non e' una deduzione: e' anche cio' che rende
     * possibile -display sdl, che usa gli stessi shader su un contesto desktop. */
    if (winq_wgl_attivo()) {
        /* QUI la finestra esiste -- lo prova la riga sotto nel percorso EGL, che passa
         * winq.hwnd a eglCreateWindowSurface -- quindi qui si crea il contesto. */
        if (winq_wgl_init(winq.hwnd)) {
            goto shader;
        }

        /* IL RIPIEGO TARDIVO, e non e' una comodita'. Da quando WGL e' il default, un
         * driver che non concede un profilo core non deve impedire l'avvio. EGL si puo'
         * ancora inizializzare qui: egl_init non ha bisogno della finestra -- solo la
         * superficie ne ha -- ed e' per questo che winq_mode viene ricordato. */
        {
            Error *err2 = NULL;

            winq_wgl_rinuncia();
            error_printf("winq: desktop context not obtained, falling back to ANGLE. "
                         "The guest will stay on OpenGL ES 2.0.\n");
            if (!egl_init(NULL, winq_mode, &err2)) {
                error_reportf_err(err2, "winq: and EGL does not start either: ");
                error_printf("winq: check libEGL.dll and libGLESv2.dll in "
                             "runtime/bin, or use -display sdl.\n");
                return false;
            }
        }
    }

    /* GLES 3.0 e non 2.0, deliberatamente. qemu_egl_init_ctx()
     * (ui/egl-helpers.c:672-700) crea il contesto radice con
     * EGL_CONTEXT_CLIENT_VERSION 2, e con un contesto ES 2.0 due cose che
     * servono qui non esistono: glBlitFramebuffer, usata da egl_fb_blit
     * (ui/egl-helpers.c:141-172), e gli shader di ui/shader/ (i .vert e i
     * .frag), che dichiarano "#version 300 es". Sbagliare qui darebbe una
     * finestra nera senza errori, quindi il contesto lo si chiede esplicito e,
     * se ANGLE non lo da', si dice perche' invece di disegnare nulla.
     * Si passa da qemu_egl_create_context (ui/egl-context.c:5-26), la stessa
     * funzione che serve virglrenderer qui sotto, cosi' il contesto di
     * presentazione e quelli del ponte finiscono nello stesso share group. */
    static QEMUGLParams params = { .major_ver = 3, .minor_ver = 0 };

    winq_ectx = qemu_egl_create_context(&winq.dgc, &params, qemu_egl_rn_ctx);
    if (!winq_ectx) {
        error_report("winq: no GLES 3.0 context from ANGLE (%s). "
                     "It is needed for glBlitFramebuffer and for the shaders "
                     "\"#version 300 es\" of ui/shader/. Use -display sdl.",
                     qemu_egl_get_error_string());
        return false;
    }

    /* Il nome dice x11 ma la funzione non ha nulla di X11: e' un
     * eglCreateWindowSurface + eglMakeCurrent su EGLNativeWindowType, senza
     * alcun #ifdef, ed e' compilata sempre (ui/egl-helpers.c:469-489, dichiarata
     * senza guardie in include/ui/egl-helpers.h:62). Su Windows
     * EGLNativeWindowType e' HWND. Si usa questa invece di scrivere le tre
     * righe di EGL a mano: qemu_egl_init_surface_win32, che il brief citava,
     * non esiste in 11.0.3 -- verificato con grep su tutto l'albero. */
    winq_esurface = qemu_egl_init_surface_x11(winq_ectx,
                                              (EGLNativeWindowType)winq.hwnd);
    if (!winq_esurface) {
        error_report("winq: creating the EGL surface on the window "
                     "failed (%s). Use -display sdl.",
                     qemu_egl_get_error_string());
        return false;
    }

    /* Lo shader serve solo al percorso 2D (surface_gl_render_texture): il
     * percorso di scanout usa glBlitFramebuffer e non ne ha bisogno. Si crea
     * comunque qui, una volta, perche' crearlo dentro il primo gfx_switch
     * vorrebbe dire compilare shader in mezzo a un aggiornamento del guest. */
shader:
    winq_gls = qemu_gl_init_shader();
    if (!winq_gls) {
        error_report("winq: qemu_gl_init_shader failed. Use -display sdl.");
        return false;
    }

    info_report("winq: %s ready, GL_VENDOR=%s GL_RENDERER=%s GL_VERSION=%s",
                winq_wgl_attivo() ? "WGL" : "EGL",
                (const char *)glGetString(GL_VENDOR),
                (const char *)glGetString(GL_RENDERER),
                (const char *)glGetString(GL_VERSION));
    return true;
}

/* Rende corrente il nostro contesto sulla nostra superficie. Va rifatto a ogni
 * ingresso: fra due nostre callback virglrenderer rende corrente uno dei propri
 * contesti (dpy_gl_ctx_make_current), quindi non si puo' assumere quale sia
 * corrente adesso. E' lo stesso motivo per cui ui/sdl2-gl.c rifa
 * SDL_GL_MakeCurrent in cima a ogni funzione. */
static bool winq_corrente(void)
{
    if (winq_wgl_attivo()) {
        return winq_wgl_corrente();
    }

    if (!winq_esurface) {
        return false;
    }
    if (!eglMakeCurrent(qemu_egl_display, winq_esurface, winq_esurface,
                        winq_ectx)) {
        error_report("winq: eglMakeCurrent failed: %s",
                     qemu_egl_get_error_string());
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Le quattro operazioni di contesto                                  */

static bool winq_gl_is_compatible_dcl(DisplayGLCtx *dgc,
                                      DisplayChangeListener *dcl)
{
    /* Solo il nostro listener sa disegnare cio' che questo contesto produce.
     * Copia di sdl2_gl_is_compatible_dcl (ui/sdl2.c:831-836). */
    return dcl->ops == &winq_dcl_ops;
}

static QEMUGLContext winq_gl_ctx_create(DisplayGLCtx *dgc,
                                        QEMUGLParams *params)
{
    /* qemu_egl_rn_ctx come share context: e' questa riga che mette la texture
     * di scanout nello stesso share group della nostra superficie, e quindi
     * decide che non serve nessuna copia. Identica a
     * ui/egl-headless.c:42-46 e ui/spice-display.c:1043. */
    QEMUGLContext ctx;

    if (winq_wgl_attivo()) {
        return winq_wgl_ctx_create(dgc, params);
    }

    ctx = qemu_egl_create_context(dgc, params, qemu_egl_rn_ctx);

    if (!ctx) {
        /* MISURATO su questa macchina, e non e' un guasto: virglrenderer
         * chiede in sequenza contesti OpenGL desktop 4.6, 4.5, ... 3.2, e
         * ANGLE sa fare solo GL ES, quindi qemu_egl_create_context li traduce
         * in versioni ES che non esistono (ES 4.x) o che ANGLE su D3D11 non
         * offre (ES 3.2, il massimo e' 3.1): tutte EGL_BAD_MATCH. Fallire e'
         * la risposta giusta -- restituire un contesto ES facendolo passare
         * per un desktop 3.2 farebbe usare a vrend funzioni che non ci sono.
         * virglrenderer se ne accorge e ripiega sul proprio percorso EGL,
         * usando qemu_egl_display che gli passa
         * hw/display/virtio-gpu-virgl.c:1445-1447; da lui poi arrivano le
         * texture di scanout che presentiamo. Il prezzo di quel ripiego --
         * GLES del guest da 3.1 a 2.0 -- e' misurato e riferito nel rapporto
         * di quella verifica: non e' aggirabile da questo file.
         * Si stampa una volta sola: virglrenderer ritenta l'intera scala a
         * ogni inizializzazione, e venti righe identiche coprirebbero
         * qualunque messaggio utile. */
        static bool detto;

        if (!detto) {
            detto = true;
            error_report("winq: ANGLE gives no desktop OpenGL contexts (the "
                         "first virglrenderer request was %d.%d: %s). "
                         "virglrenderer falls back to its own EGL path; "
                         "presentation works, but the guest gets GLES 2.0 "
                         "instead of 3.1. For the full desktop GL chain "
                         "use -display sdl.",
                         params->major_ver, params->minor_ver,
                         qemu_egl_get_error_string());
        }
    }
    return ctx;
}

/* Involucri che scelgono la strada. Non si duplicano le tabelle delle ops perche'
 * winq_dcl_ops e winq_gl_ctx_ops sono const e riferite altrove: un bivio dentro
 * quattro funzioni di tre righe costa meno di due tabelle da tenere in pari. */
static void winq_ctx_destroy(DisplayGLCtx *dgc, QEMUGLContext ctx)
{
    if (winq_wgl_attivo()) {
        winq_wgl_ctx_destroy(dgc, ctx);
    } else {
        qemu_egl_destroy_context(dgc, ctx);
    }
}

static int winq_ctx_make_current(DisplayGLCtx *dgc, QEMUGLContext ctx)
{
    if (winq_wgl_attivo()) {
        return winq_wgl_make_current(dgc, ctx);
    }
    return qemu_egl_make_context_current(dgc, ctx);
}

const DisplayGLCtxOps winq_gl_ctx_ops = {
    .dpy_gl_ctx_is_compatible_dcl = winq_gl_is_compatible_dcl,
    .dpy_gl_ctx_create            = winq_gl_ctx_create,
    /* Le altre due sono gia' scritte in QEMU e vanno bene cosi' come sono:
     * ui/egl-context.c:28-31 e :33-43. Riscriverle sarebbe copiare due righe. */
    .dpy_gl_ctx_destroy           = winq_ctx_destroy,
    .dpy_gl_ctx_make_current      = winq_ctx_make_current,
};

/* ------------------------------------------------------------------ */
/* Presentazione                                                      */

/* Il rettangolo in cui disegnare, con le bande nere dove le proporzioni non
 * coincidono. Lo stesso rettangolo che winq_win_to_guest usa per scartare i
 * tocchi nelle bande, perche' viene dalla stessa funzione (winq-coord.c):
 * immagine e tocco concordano per costruzione, non per revisione.
 *
 * L'origine y di glViewport e' in basso, quella della finestra in alto: qui non
 * cambia nulla perche' la banda sopra e quella sotto sono uguali per
 * costruzione (il rettangolo e' centrato). Se un giorno il centraggio diventasse
 * asimmetrico, questa e' la riga da correggere. */
/* MISURATO, e non era previsto: dopo un ridimensionamento il primo frame
 * presentato e' sbagliato di scala. ANGLE ricrea la catena di scambio della
 * superficie EGL quando si accorge che la finestra e' cambiata, e se ne accorge
 * dentro eglSwapBuffers, cioe' DOPO che noi abbiamo gia' disegnato: il primo
 * disegno finisce nel backbuffer vecchio, che poi viene mostrato scalato nella
 * finestra nuova. Sintomo osservato: due ridisegni consecutivi, senza che il
 * guest abbia prodotto un solo frame in mezzo, danno due immagini di scala
 * diversa.
 *
 * Il rimedio e' chiedere a EGL quanto e' grande la superficie DAVVERO, e se non
 * coincide ancora con la finestra rimettere in coda un ridisegno. Converge al
 * giro dopo, perche' e' proprio lo swap appena fatto ad aver adeguato la
 * superficie. Il limite di tentativi c'e' perche' se per qualche motivo le due
 * misure non coincidessero mai (per esempio uno scarto di scalatura DPI), senza
 * limite si ridisegnerebbe a 30 Hz per sempre a guest fermo: meglio
 * un'immagine imperfetta che una GPU occupata per niente. */
/* Il contatore vive qui, non in WinqState, perche' e' un dettaglio di questo
 * modulo: chi ridimensiona non deve sapere che esiste un limite di tentativi, solo
 * che comincia un ridimensionamento nuovo. Da qui la funzione qui sotto. */
static int winq_riarmi;

/* Azzera il limite di tentativi: da chiamare quando la finestra cambia dimensione,
 * cioe' quando comincia una convergenza NUOVA.
 *
 * MISURATO dalla review finale del branch, e non da noi: senza questo, `winq_riarmi`
 * era un chiavistello di PROCESSO invece che di ridimensionamento. Si azzerava solo
 * quando le due misure coincidevano, quindi tre ridimensionamenti sfortunati in
 * TUTTA la vita del processo spegnevano il riarmo per sempre, e da quel momento il
 * frame finale di ogni trascinamento poteva restare di scala sbagliata fino al
 * messaggio successivo.
 *
 * Sintomo riferito dall'utente prima che se ne trovasse la causa: "il
 * ridimensionamento e' non molto funzionante". Era stato attribuito ai soli ~30 Hz
 * del ciclo di refresh -- vero, ma non tutta la verita'. */
void winq_present_nuova_dimensione(void)
{
    winq_riarmi = 0;
}

static void winq_ricontrolla_dimensione(void)
{
    EGLint sw = 0, sh = 0;

    if (!eglQuerySurface(qemu_egl_display, winq_esurface, EGL_WIDTH, &sw) ||
        !eglQuerySurface(qemu_egl_display, winq_esurface, EGL_HEIGHT, &sh)) {
        return;
    }

    if (sw == winq.geom.win_w && sh == winq.geom.win_h) {
        winq_riarmi = 0;
        return;
    }
    if (getenv("WINQ_DIAG_RESIZE")) {
        info_report("winq resize: MISMATCH -- surface %dx%d against window "
                    "%dx%d, riarmi %d%s", (int)sw, (int)sh,
                    winq.geom.win_w, winq.geom.win_h, winq_riarmi,
                    winq_riarmi < 3 ? "" : " (EXHAUSTED: no more redraws)");
    }
    if (winq_riarmi < 3) {
        winq_riarmi++;
        winq.expose = true;
    }
}

/* IL VIEWPORT SI CALCOLA SULLA SUPERFICIE EGL, NON SULLA FINESTRA. La differenza
 * conta solo durante un ridimensionamento, ed e' esattamente il momento in cui si
 * vedeva il difetto.
 *
 * MISURATO, riferito dall'utente: "il ridimensionamento ha sempre il primo frame
 * sbagliato". Causa: ANGLE ricrea la catena di scambio dentro eglSwapBuffers, cioe'
 * DOPO che abbiamo disegnato. Calcolando il viewport su winq.geom -- la dimensione
 * della finestra, che WM_SIZE ha gia' aggiornato -- si disegnava per una superficie
 * piu' grande o piu' piccola di quella che c'era davvero, e il risultato appariva
 * scalato male. winq_ricontrolla_dimensione se ne accorgeva e rimetteva in coda un
 * ridisegno, ma DOPO: un frame fuori scala si vedeva sempre.
 *
 * Chiedendo la dimensione PRIMA di disegnare non si sbaglia mai la scala. Al massimo
 * per un giro si vede il contenuto alla dimensione vecchia, che e' corretto e non
 * deformato -- e il riarmo fa convergere la geometria al giro dopo.
 *
 * Il TOCCO continua a usare winq.geom, e deve: le coordinate di WM_POINTER sono in
 * spazio client della finestra. Durante un ridimensionamento i due possono
 * discordare per un giro, e un tocco in quell'istante cade di pochi pixel -- prezzo
 * accettabile contro un'immagine deformata a ogni trascinamento.
 *
 * Se eglQuerySurface fallisce si ricade su winq.geom: e' la stima migliore che
 * abbiamo, ed e' anche cio' che si faceva prima. */
static void winq_viewport(int *x, int *y, int *w, int *h)
{
    WinqGeom g = winq.geom;
    double dx, dy, dw, dh;
    EGLint sw = 0, sh = 0;

    if (winq_wgl_attivo()) {
        int ww, wh;
        /* Con WGL il framebuffer di default E' l'area cliente, e GetClientRect la da'
         * senza intermediari: non c'e' la catena di scambio di ANGLE che si ricrea
         * dentro lo swap, cioe' dopo che abbiamo disegnato. */
        if (winq_wgl_dimensione(&ww, &wh)) {
            g.win_w = ww;
            g.win_h = wh;
        }
    } else if (winq_esurface != EGL_NO_SURFACE &&
        eglQuerySurface(qemu_egl_display, winq_esurface, EGL_WIDTH, &sw) &&
        eglQuerySurface(qemu_egl_display, winq_esurface, EGL_HEIGHT, &sh) &&
        sw > 0 && sh > 0) {
        g.win_w = sw;
        g.win_h = sh;
    }

    if (getenv("WINQ_DIAG_RESIZE")) {
        static int pw, ph, psw, psh;

        if (pw != winq.geom.win_w || ph != winq.geom.win_h ||
            psw != (int)sw || psh != (int)sh) {
            pw = winq.geom.win_w;
            ph = winq.geom.win_h;
            psw = (int)sw;
            psh = (int)sh;
            info_report("winq resize: window %dx%d surface %dx%d "
                        "guest %dx%d riarmi %d",
                        winq.geom.win_w, winq.geom.win_h, (int)sw, (int)sh,
                        winq.geom.guest_w, winq.geom.guest_h, winq_riarmi);
        }
    }

    if (!winq_guest_rect(&g, &dx, &dy, &dw, &dh)) {
        *x = 0;
        *y = 0;
        *w = g.win_w > 0 ? g.win_w : 1;
        *h = g.win_h > 0 ? g.win_h : 1;
        return;
    }
    *x = (int)(dx + 0.5);
    *y = (int)(dy + 0.5);
    *w = (int)(dw + 0.5);
    *h = (int)(dh + 0.5);
}

/* Conta i frame e ne stampa uno ogni potenza di dieci: 1, 10, 100, 1000...
 * Al massimo una manciata di righe per sessione, e sono l'unica prova
 * misurabile che i frame arrivano davvero -- dallo schermo non si distingue
 * "image still because the guest is still" da "presentation dead". */
static void winq_conta_frame(const char *percorso)
{
    uint64_t n = ++winq_frames;
    uint64_t p = 1;

    while (p < n) {
        p *= 10;
    }
    if (p == n) {
        info_report("winq: frame %" PRIu64 " presented (%s)", n, percorso);
    }
}

/* La prova, a runtime, che la texture del guest e' usabile dal nostro contesto.
 * Stampata una volta sola, alla prima texture di scanout.
 *
 * Come si legge: egl_fb_setup_for_tex ha appena attaccato backing_id come
 * COLOR_ATTACHMENT0 di un FBO creato NEL NOSTRO contesto. Se l'id non
 * appartenesse al nostro share group, glIsTexture darebbe 0 e lo stato dell'FBO
 * non sarebbe COMPLETE (0x8CD5): sarebbe INCOMPLETE_ATTACHMENT (0x8CD6). E'
 * questo il caso in cui, dice la spec, winq-present.c va ripensato -- e il
 * sintomo altrimenti sarebbe una finestra nera senza un solo errore. */
static void winq_prova_texture(uint32_t backing_id, void *d3d_tex2d)
{
    GLenum stato = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    GLboolean nostra = glIsTexture(backing_id);
    GLenum errore = glGetError();

    info_report("winq: first scanout texture id=%u: glIsTexture=%d "
                "fbo_status=0x%x (0x8CD5=COMPLETE) glGetError=0x%x "
                "d3d_tex2d=%p",
                backing_id, (int)nostra, (unsigned)stato, (unsigned)errore,
                d3d_tex2d);

    if (!nostra || stato != GL_FRAMEBUFFER_COMPLETE) {
        error_report("winq: the guest scanout texture is NOT usable "
                     "from the presentation context: the share groups do not "
                     "match. The window would stay black with no other "
                     "errors. Use -display sdl and report it.");
    }
}

static void winq_modo_scanout(bool acceso)
{
    if (winq_scanout_mode == acceso) {
        return;
    }
    winq_scanout_mode = acceso;
    if (!acceso) {
        /* Uscendo dallo scanout la texture del guest non e' piu' nostra da
         * guardare, e la superficie 2D va ricreata: copia di
         * sdl2_set_scanout_mode (ui/sdl2-gl.c:35-49). */
        egl_fb_destroy(&winq_guest_fb);
        if (winq_ds) {
            surface_gl_destroy_texture(winq_gls, winq_ds);
            surface_gl_create_texture(winq_gls, winq_ds);
            winq_updates++;
        }
    }
}

void winq_present_scanout_texture(DisplayChangeListener *dcl,
                                  uint32_t backing_id,
                                  bool backing_y_0_top,
                                  uint32_t backing_width,
                                  uint32_t backing_height,
                                  uint32_t x, uint32_t y,
                                  uint32_t w, uint32_t h,
                                  void *d3d_tex2d)
{
    static bool provata;

    if (!winq_corrente()) {
        return;
    }

    winq_y0_top = backing_y_0_top;
    /* w/h e non backing_w/h: e' il pezzo di texture che si vede, ed e' lo
     * spazio in cui il guest aspetta le coordinate del tocco. */
    winq.geom.guest_w = w;
    winq.geom.guest_h = h;

    /* L'INVARIANTE CHE QUESTE DUE RIGHE ASSUMONO, ora dichiarato.
     *
     * guest_w/h prendono lo scanout VISIBILE (w/h), mentre egl_fb_blit qui sotto
     * usa il backing INTERO come rettangolo sorgente (backing_width/height) --
     * e' cosi' anche in ui/sdl2-gl.c, da cui questo percorso e' copiato. Le due
     * cose coincidono solo se il device annuncia uno scanout che copre tutto il
     * suo backing, con origine in (0,0).
     *
     * Se non coincidessero, immagine e tocco divergerebbero in direzioni
     * OPPOSTE: si disegnerebbe l'intero backing scalato nella finestra, mentre le
     * coordinate del dito verrebbero mappate sul solo rettangolo visibile. Un
     * difetto simmetrico e' il piu' difficile da riconoscere guardando lo schermo,
     * perche' tutto sembra a posto tranne il punto dove il dito atterra.
     *
     * Con virtio-gpu-gl non si e' mai osservato. Ma la review finale ha rilevato
     * che l'invariante era ASSUNTO senza nemmeno un controllo, e un'assunzione
     * taciuta e' quella che il prossimo lettore non sa di dover verificare. Si
     * avvisa una volta invece di asserire: un avviso lascia l'emulatore usabile e
     * nomina la causa, un abort trasformerebbe una divergenza di coordinate in
     * una VM che muore. */
    if (x != 0 || y != 0 || w != backing_width || h != backing_height) {
        warn_report_once("winq: the visible scanout (%ux%u at +%u+%u) does not cover "
                         "the whole backing (%ux%u). Drawing uses the backing while "
                         "touch uses the scanout: the coordinates will diverge.",
                         w, h, x, y, backing_width, backing_height);
    }

    if (!provata) {
        /* Si svuota la coda degli errori GL PRIMA di legare la texture, cosi'
         * il glGetError della prova dice se e' stato il nostro binding a
         * sbagliare e non un errore lasciato in coda da qualcun altro. Senza
         * questo la prima misura riportava GL_INVALID_ENUM (0x500) che non
         * veniva da qui, e si sarebbe potuto concludere il contrario del vero. */
        while (glGetError() != GL_NO_ERROR) {
            /* nulla: si scarta */
        }
    }

    winq_modo_scanout(true);
    egl_fb_setup_for_tex(&winq_guest_fb, backing_width, backing_height,
                         backing_id, false);

    if (!provata) {
        provata = true;
        winq_prova_texture(backing_id, d3d_tex2d);
    }
}

/* Vero quando il guest sta presentando dal proprio scanout 3D, cioe' quando la
 * risoluzione in winq.geom e' quella che il driver del guest ha PROGRAMMATO -- non il
 * framebuffer di avvio che QEMU mostra prima, ne' i segnaposto tipo "Display output is
 * not active".
 *
 * Serve a winq_annuncia_frequenza, e la ragione e' un difetto misurato: annunciando la
 * geometria del percorso 2D si diceva al guest "sei 640x480", che era la dimensione del
 * framebuffer di avvio, e Android ci restava -- "wm size" dava "Physical size: 640x480".
 * QemuUIInfo porta geometria e frequenza insieme e non si puo' mandare solo la seconda:
 * con larghezza o altezza a zero virtio-gpu DISABILITA l'uscita
 * (hw/display/virtio-gpu-base.c:116-120). */
bool winq_present_in_scanout(void)
{
    return winq_scanout_mode;
}

void winq_present_scanout_disable(DisplayChangeListener *dcl)
{
    if (!winq_corrente()) {
        return;
    }
    winq_modo_scanout(false);
}

/* winq: IL COSTO DELLA PRESENTAZIONE, dietro WINQ_GPU_DIAG.
 *
 * PERCHE' ESISTE. Lo shim che conta le chiamate GL vive dentro virglrenderer;
 * la presentazione sta qui, in QEMU, e nessuna diagnostica l'ha mai contata.
 * E' l'unico lavoro GL che avviene una volta per fotogramma senza essere mai
 * stato misurato -- un punto cieco, non un sospetto scartato.
 *
 * Vale la pena guardarci perche' due ipotesi solide sono gia' cadute con una
 * misura: gli Unlock del kernel NON sono i cambi di contesto (tagliati quattro
 * volte, unlock giu' solo di 1,5) e NON sono ricambio di risorse (create e
 * unref a zero sotto carico, cinque identificatori fissi). Restano 25 Unlock e
 * 3 paginazioni per fotogramma senza padrone.
 *
 * Le tre voci misurate sono le tre cose che questo percorso fa per frame:
 *   - corrente: winq_corrente(), che e' un wglMakeCurrent verso il contesto
 *     di presentazione. Da notare che e' un contesto DIVERSO da quelli di
 *     virglrenderer, quindi ogni frame ne aggiunge uno che il conteggio
 *     dall'altra parte non vede;
 *   - blit: egl_fb_setup_default + egl_fb_blit, cioe' glClear + glBlitFramebuffer;
 *   - swap: SwapBuffers, che su D3D12 puo' costare parecchio.
 *
 * Orologio: g_get_monotonic_time(), microsecondi -- basta per grandezze di
 * decine o centinaia di microsecondi, e non introduce dipendenze nuove. */
static int64_t winq_pres_corrente_us, winq_pres_blit_us, winq_pres_swap_us;
static unsigned long winq_pres_giri;
static int64_t winq_pres_inizio;

static bool winq_pres_diag(void)
{
    static int acceso = -1;

    if (acceso < 0) {
        acceso = getenv("WINQ_GPU_DIAG") ? 1 : 0;
    }
    return acceso == 1;
}

static void winq_pres_riepilogo(int64_t ora)
{
    if (!winq_pres_inizio) {
        winq_pres_inizio = ora;
        return;
    }
    if (ora - winq_pres_inizio < 1000000) {
        return;
    }
    error_report("winq-present/s: frame=%lu corrente=%lld us blit=%lld us "
                 "swap=%lld us totale=%lld us",
                 winq_pres_giri, (long long)winq_pres_corrente_us,
                 (long long)winq_pres_blit_us, (long long)winq_pres_swap_us,
                 (long long)(winq_pres_corrente_us + winq_pres_blit_us +
                             winq_pres_swap_us));
    winq_pres_corrente_us = winq_pres_blit_us = winq_pres_swap_us = 0;
    winq_pres_giri = 0;
    winq_pres_inizio = ora;
}

static void winq_disegna_scanout(void)
{
    int vx, vy, vw, vh;

    if (!winq_guest_fb.framebuffer) {
        return;   /* nessuna texture ancora: la finestra resta come e' */
    }
    {
        int64_t t0 = winq_pres_diag() ? g_get_monotonic_time() : 0;
        bool ok = winq_corrente();

        if (t0) {
            winq_pres_corrente_us += g_get_monotonic_time() - t0;
        }
        if (!ok) {
            return;
        }
    }

    winq_viewport(&vx, &vy, &vw, &vh);
    /* egl_fb_setup_default(fb, w, h, x, y) descrive il framebuffer di default
     * (id 0, cioe' la finestra) e il rettangolo di destinazione; egl_fb_blit
     * (ui/egl-helpers.c:141-172) fa glClear -- che pulisce le bande -- e poi
     * glBlitFramebuffer scalando dentro quel rettangolo. Nessuno shader e
     * nessuna copia in memoria: e' lo stesso percorso di
     * sdl2_gl_scanout_flush (ui/sdl2-gl.c:233-254), a cui aggiungiamo solo le
     * bande (SDL usa 0,0 e deforma). */
    {
        int64_t t0 = winq_pres_diag() ? g_get_monotonic_time() : 0;

        egl_fb_setup_default(&winq_win_fb, vw, vh, vx, vy);
        egl_fb_blit(&winq_win_fb, &winq_guest_fb, !winq_y0_top);
        if (t0) {
            winq_pres_blit_us += g_get_monotonic_time() - t0;
        }
    }

    /* DIAGNOSTICA TEMPORANEA, per il difetto "all-white image" su WGL.
     *
     * Misura ai CONFINI invece di una congettura per avvio. Il pixel riletto e' la
     * riga decisiva: dice se il blit ha scritto nel framebuffer della finestra. Bianco
     * puro significa back buffer mai toccato, perche' glClear in un contesto nuovo usa
     * nero -- quindi il bianco non puo' venire da noi. Le altre righe dicono quale
     * confine ha ceduto. */
    if (getenv("WINQ_DIAG_WGL")) {
        static int chiamate;

        /* Si campiona anche DOPO l'avvio, e non solo i primi frame: durante l'avvio lo
         * schermo del guest e' legittimamente nero, quindi un pixel nero nei primi tre
         * frame non distingue "the blit writes nothing" da "non c'e' ancora niente da
         * scrivere". La prima versione campionava solo quelli, e ha prodotto tre righe
         * identiche che non decidevano nulla. */
        chiamate++;
        if (chiamate <= 3 || chiamate % 300 == 0) {
            GLenum err = glGetError();
            GLint fb_letto = 0, fb_scritto = 0;
            GLenum st_r, st_w;
            unsigned char px[4] = {9, 9, 9, 9};

            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fb_letto);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb_scritto);
            st_r = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
            st_w = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
            /* SI RILEGA IL READ FRAMEBUFFER A 0 PRIMA DI LEGGERE, e la prima
             * versione di questa diagnostica non lo faceva: egl_fb_blit lascia
             * GL_READ_FRAMEBUFFER legato alla SORGENTE, quindi glReadPixels leggeva
             * la texture del guest credendo di leggere la finestra. Una misura che
             * misura la cosa sbagliata e' peggio di nessuna misura.
             *
             * Il centro del rettangolo di DESTINAZIONE, non della finestra: nelle
             * bande nere si leggerebbe nero anche a blit riuscito. */
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadPixels(vx + vw / 2, vy + vh / 2, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, px);
            info_report("winq/diag: frame %d blit err=0x%x letto=%d(st 0x%x) "
                        "scritto=%d(st 0x%x) dest=%d,%d %dx%d "
                        "guest_fb=%u %ux%u pixel=%u,%u,%u,%u err_letto=0x%x",
                        chiamate, err, fb_letto, st_r, fb_scritto, st_w,
                        vx, vy, vw, vh,
                        winq_guest_fb.framebuffer,
                        winq_guest_fb.width, winq_guest_fb.height,
                        px[0], px[1], px[2], px[3], glGetError());
        }
    }
    {
        int64_t t0 = winq_pres_diag() ? g_get_monotonic_time() : 0;

        if (winq_wgl_attivo()) {
            winq_wgl_swap();
        } else {
            eglSwapBuffers(qemu_egl_display, winq_esurface);
        }
        if (t0) {
            int64_t ora = g_get_monotonic_time();

            winq_pres_swap_us += ora - t0;
            winq_pres_giri++;
            winq_pres_riepilogo(ora);
        }
    }
    winq_ricontrolla_dimensione();
    winq_conta_frame("scanout");
}

/* .dpy_gl_update: e' qui che il device dice "the frame is ready, show it".
 * Stesso ruolo di sdl2_gl_scanout_flush. */
void winq_present_gl_update(DisplayChangeListener *dcl,
                            uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!winq_scanout_mode) {
        return;
    }
    winq_disegna_scanout();
}

/* ------------------------------------------------------------------ */
/* Percorso 2D: la superficie che QEMU disegna in memoria.
 *
 * Serve anche con virtio-gpu-gl: prima che il driver del guest attivi lo
 * scanout 3D, e in ogni momento in cui QEMU sostituisce la superficie con un
 * segnaposto ("Display output is not active", "This VM has no graphic display
 * device" -- ui/console.c:264-278). Senza questo percorso la finestra sarebbe
 * nera in tutti quei momenti, cioe' proprio quando serve leggere un messaggio.
 * E' la copia del ramo non-scanout di ui/sdl2-gl.c. */

/* Vero quando winq_ds e' stata sostituita senza che si potesse creare la sua
 * texture, perche' il contesto non era corrente. Si ritenta al primo uso utile.
 *
 * PERCHE' ESISTE. Trovato dalla review finale. Il ramo d'errore di gfx_switch
 * sostituiva winq_ds e tornava: la texture della superficie vecchia restava
 * allocata senza nessuno che la nominasse, e la nuova non ne aveva nessuna. Da
 * quel momento surface_gl_update_texture lavorava su una superficie con
 * texture 0 e qemu_gl_run_texture_blit disegnava il nulla: FINESTRA NERA
 * PERMANENTE, senza un solo errore stampato, e l'unica uscita era un
 * scanout_disable che rifacesse il giro.
 *
 * Non si puo' semplicemente rifiutare il cambio: la superficie vecchia sta per
 * essere liberata da QEMU e tenerne il puntatore darebbe un uso dopo la
 * liberazione, che e' peggio di una finestra nera. Si accetta il cambio e si
 * rimanda il lavoro GL, che e' l'unica parte impossibile senza contesto. */
static bool winq_ds_senza_texture;

/* Crea la texture rimandata, se ce n'e' una in attesa e ora si puo'. Chiamata da
 * ogni percorso che stia per USARE winq_ds: e' il punto in cui avere una texture
 * diventa necessario, e quindi il punto giusto per assicurarsene. */
static void winq_ds_recupera(void)
{
    if (!winq_ds_senza_texture || !winq_ds || !winq_corrente()) {
        return;
    }
    if (!winq_scanout_mode) {
        winq.geom.guest_w = surface_width(winq_ds);
        winq.geom.guest_h = surface_height(winq_ds);
    }
    surface_gl_create_texture(winq_gls, winq_ds);
    winq_ds_senza_texture = false;
    winq_updates++;
    info_report("winq: 2D surface texture created late, "
                "the context was not current at the surface change");
}

void winq_present_gfx_switch(DisplayChangeListener *dcl,
                             DisplaySurface *new_surface)
{
    if (!winq_corrente()) {
        /* Senza contesto non si distrugge la texture vecchia: la perdita e'
         * inevitabile e vale una superficie, mentre il puntatore va aggiornato
         * comunque per non tenerne uno che QEMU sta liberando. */
        winq_ds = new_surface;
        winq_ds_senza_texture = (new_surface != NULL);
        return;
    }

    surface_gl_destroy_texture(winq_gls, winq_ds);
    winq_ds = new_surface;
    winq_ds_senza_texture = false;
    if (!winq_ds) {
        return;
    }

    if (!winq_scanout_mode) {
        winq.geom.guest_w = surface_width(winq_ds);
        winq.geom.guest_h = surface_height(winq_ds);
    }
    surface_gl_create_texture(winq_gls, winq_ds);
    winq_updates++;
}

void winq_present_gfx_update(DisplayChangeListener *dcl,
                             int x, int y, int w, int h)
{
    if (!winq_ds || !winq_corrente()) {
        return;
    }
    winq_ds_recupera();
    surface_gl_update_texture(winq_gls, winq_ds, x, y, w, h);
    winq_updates++;
}

static void winq_disegna_superficie(void)
{
    if (!winq_ds || !winq_corrente()) {
        return;
    }
    winq_ds_recupera();

    /* surface_gl_setup_viewport (ui/console-gl.c:191-212) calcola da se' le
     * bande, con la stessa regola di winq_guest_rect. */
    /* Si torna esplicitamente al framebuffer della finestra: uscendo dallo
     * scanout resta legato l'FBO della texture del guest, e disegnare la'
     * dentro non darebbe alcun errore -- solo una finestra nera. */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    surface_gl_setup_viewport(winq_gls, winq_ds,
                              winq.geom.win_w, winq.geom.win_h);
    /* qemu_gl_run_texture_blit disegna la texture LEGATA ADESSO, non una
     * texture passata come argomento (ui/shader.c:73-80): fra il nostro
     * update e questo disegno virglrenderer puo' aver legato altro nel suo
     * contesto, quindi si rilega qui invece di fidarsi. */
    glBindTexture(GL_TEXTURE_2D, winq_ds->texture);
    surface_gl_render_texture(winq_gls, winq_ds);
    if (winq_wgl_attivo()) {
        winq_wgl_swap();
    } else {
        eglSwapBuffers(qemu_egl_display, winq_esurface);
    }
    winq_ricontrolla_dimensione();
    winq_conta_frame("2D surface");
}

/* ------------------------------------------------------------------ */

/* Chiamata dal .dpy_refresh. Non ridisegna a ogni giro di orologio: in
 * scanout il frame lo annuncia il device con .dpy_gl_update, e ridisegnare a
 * 33 Hz senza che il guest abbia prodotto nulla costerebbe GPU per niente --
 * stessa scelta di sdl2_gl_refresh (ui/sdl2-gl.c:112-124), che presenta solo
 * se scon->updates. Resta il caso della finestra ridimensionata o riesposta,
 * dove il contenuto e' lo stesso ma il rettangolo e' cambiato. */
void winq_present_frame(void)
{
    bool riesposta = winq.expose;

    winq.expose = false;

    if (winq_scanout_mode) {
        if (riesposta) {
            winq_disegna_scanout();
        }
        return;
    }
    if (winq_updates || riesposta) {
        winq_updates = 0;
        winq_disegna_superficie();
    }
}
