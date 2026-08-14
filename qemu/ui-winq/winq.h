/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq.h -- stato condiviso fra i moduli del backend -display winq.
 *
 * Include sia <windows.h> sia header di QEMU: e' una combinazione con
 * conflitti noti (windows.h definisce macro come "interface", "ERROR",
 * "min"/"max" che collidono con simboli usati altrove nei sorgenti QEMU).
 * La convenzione dell'albero per evitarlo, vista in
 * include/qemu/thread-win32.h e include/system/whpx-internal.h, non e'
 * "non includere mai windows.h in un header": e' che qemu/osdep.h, che
 * OGNI file .c di QEMU include per primo (regola di tutto l'albero, non
 * nostra), su _WIN32 include gia' <windows.h> (via
 * include/system/os-win32.h), dopo aver definito WIN32_LEAN_AND_MEAN.
 * Quando questo header viene raggiunto -- sempre dopo osdep.h, mai prima,
 * perche' winq-window.c include osdep.h come prima riga -- <windows.h> e'
 * gia' stato incluso: il #include qui sotto e' un no-op bloccato dalla sua
 * stessa guardia (_WINDOWS_), e nessuna macro di windows.h finisce a
 * scontrarsi con un header QEMU parsato prima, perche' non ce n'e' nessuno:
 * e' il primo. Stessa garanzia di cui gode ui/win32-kbd-hook.c, che infatti
 * non include windows.h da solo e usa "void *hwnd" invece di HWND nel
 * proprio header (include/ui/win32-kbd-hook.h) proprio per non impegnarsi
 * sull'ordine quando non serve. Qui HWND serve nello stato condiviso, quindi
 * si include, ma SEMPRE dopo osdep.h nel .c che lo consuma.
 */
#ifndef WINQ_H
#define WINQ_H
#include <windows.h>
#include "ui/console.h"
#include "winq-coord.h"

typedef struct WinqState {
    HWND hwnd;
    WinqGeom geom;         /* aggiornata a ogni WM_SIZE e a ogni switch del guest */
    DisplayChangeListener dcl;
    DisplayGLCtx dgc;      /* le quattro DisplayGLCtxOps: vedi winq-present.c */
    QemuConsole *con;
    /* La finestra ha ricevuto WM_SIZE o WM_PAINT: il contenuto corrente va
     * ridisegnato anche se il guest non ha mandato aggiornamenti. Stesso ruolo
     * di SDL_WINDOWEVENT_EXPOSED -> sdl2_redraw() in ui/sdl2.c. Senza,
     * ridimensionare la finestra a guest fermo lascia l'immagine vecchia
     * stiracchiata e sembra un guasto della presentazione. */
    bool expose;
} WinqState;

extern WinqState winq;                    /* una sola finestra nella v1 */

/* Le operazioni del DisplayChangeListener di winq. Non e' static perche'
 * winq-present.c deve poterle confrontare in dpy_gl_ctx_is_compatible_dcl,
 * esattamente come ui/sdl2.c:832-835 confronta con &dcl_gl_ops. */
extern const DisplayChangeListenerOps winq_dcl_ops;

/* Crea il thread della finestra e aspetta che la finestra esista. Ritorna
 * false se non e' stato possibile crearla, con la causa gia' riportata. Il
 * chiamante e' winq_init, sul ciclo principale, e non puo' proseguire prima:
 * winq_present_init ha bisogno dell'HWND. */
bool winq_avvia_thread(int w, int h);

/* Chiede al ciclo principale di drenare la coda appena puo'. Chiamata dal
 * thread della finestra, ed e' l'UNICA funzione di QEMU che quel thread
 * tocca: qemu_bh_schedule e' pensata per essere chiamata da qualunque thread
 * e non prende il BQL (include/qemu/aio.h). */
void winq_sveglia(void);

/* Il messaggio con cui il ciclo principale chiede alla finestra di
 * ridimensionarsi. wParam = larghezza, lParam = altezza dell'area CLIENTE.
 *
 * ESISTE PER UNA RAGIONE PRECISA, e toglierlo riaprirebbe il difetto che
 * questo lavoro chiude. SetWindowPos consegna WM_SIZE in modo SINCRONO al
 * thread proprietario della finestra: chiamarla dal ciclo principale mentre
 * quel thread e' dentro un ciclo modale lo bloccherebbe ad aspettarlo, cioe'
 * fermerebbe la macchina virtuale -- dalla porta di servizio, e sul percorso
 * della rotazione, che nessuno sospetterebbe.
 *
 * LA REGOLA GENERALE: dal ciclo principale alla finestra si POSTA, non si
 * MANDA mai. Vale per SetWindowPos, per SendMessage e per qualunque API che
 * consegni un messaggio in modo sincrono. Le LETTURE non sono coinvolte --
 * GetClientRect, GetDpiForWindow e GetDC non passano dalla coda del thread
 * proprietario e non bloccano. */
#define WINQ_MSG_DIMENSIONA (WM_APP + 1)

/* Comandi della mappa dei tasti, postati dal ciclo principale al thread della
 * finestra. SI POSTA, NON SI MANDA: un SendMessage da qui riaprirebbe
 * esattamente il blocco che il thread della finestra e' stato creato per
 * chiudere. Vedi WINQ_MSG_DIMENSIONA qui sopra per la ragione per esteso.
 *
 * WINQ_MSG_MAPPA_CARICA e WINQ_MSG_MAPPA_IMPARA portano in LPARAM una
 * stringa allocata con g_strdup da chi posta (winq-ciclo.c) e liberata con
 * g_free da chi riceve (winq-window.c). E' un passaggio di PROPRIETA', e va
 * rispettato anche nei rami di errore: dimenticarlo in un ramo perde memoria
 * a ogni comando che prende quella strada. WINQ_MSG_MAPPA_SPEGNI non porta
 * nessuna stringa, lParam vale 0. */
#define WINQ_MSG_MAPPA_CARICA  (WM_APP + 2)
#define WINQ_MSG_MAPPA_SPEGNI  (WM_APP + 3)
#define WINQ_MSG_MAPPA_IMPARA  (WM_APP + 4)

/* Cambia la risoluzione del guest e adegua la finestra. Chiamata dal tubo dei
 * comandi del guscio. */
void winq_imposta_risoluzione(int w, int h);

/* Crea la finestra topolivello, w x h client-area iniziali. Ritorna false
 * se RegisterClassEx o CreateWindowEx falliscono (errore gia' riportato
 * con error_report). */
bool winq_create_window(int w, int h);

/* --- le due funzioni che attraversano il confine fra i due file ---------
 *
 * Sono le UNICHE: winq-window.c e winq-ciclo.c si parlano attraverso queste,
 * attraverso la coda (winq-coda.h) e attraverso la struttura winq qui sopra.
 * Niente altro. Se questa lista si allunga, il confine si sta sfilacciando --
 * e' il segnale da guardare.
 *
 * Erano tre fino allo scorporo del thread: winq_dimensiona_cliente e' tornata
 * static, perche' il ciclo principale non la chiama piu' da se'. Posta
 * WINQ_MSG_DIMENSIONA e la lascia eseguire al thread proprietario della
 * finestra, per la ragione scritta su quella macro. */

/* winq-window.c. Dichiara la consapevolezza del DPI, e va chiamata PRIMA che
 * esista qualunque finestra: la dichiarazione non ha effetto retroattivo su
 * una finestra gia' creata. Il chiamante e' winq_early_init, in
 * winq-ciclo.c. */
void winq_dpi_dichiara(void);

/* winq-ciclo.c. Arma la scadenza di 30 s entro cui il guscio deve aver
 * spento Android, dopo che la chiusura gli e' stata segnalata. E' un timer
 * di QEMU, quindi vive dal lato del ciclo principale; il chiamante e' il
 * drenaggio, quando gli arriva il record di chiusura. */
void winq_arma_scadenza_chiusura(void);

/* --- winq-input.c: il tocco -------------------------------------------
 *
 * Non sotto CONFIG_OPENGL: il tocco non c'entra nulla con la presentazione, e
 * tenerlo compilato sempre fa si' che una build senza GL fallisca dove il
 * problema e' (winq_early_init, che dice "usare -display sdl") e non a caso su
 * simboli di input. */

/* Porta gli slot allo stato di riposo. OBBLIGATORIA prima del primo evento:
 * per struct touch_slot lo zero NON e' riposo, vedi il commento sulla funzione
 * in winq-input.c. Chiamata da winq_init. */
void winq_input_init(void);

/* Un solo punto d'ingresso per evento di puntatore. (wx, wy) sono coordinate
 * del CLIENT della finestra: la conversione da coordinate di schermo la fa il
 * chiamante, perche' e' lui ad avere l'HWND del messaggio. */
void winq_handle_pointer_event(uint64_t slot, int wx, int wy,
                               InputMultiTouchType type);

/* Mappa l'identificativo non contiguo di Windows su un indice di slot
 * contiguo. Ritorna INPUT_EVENT_SLOTS_MAX quando non c'e' slot: il chiamante
 * puo' passarlo comunque a winq_handle_pointer_event, che scarta. */
uint64_t winq_slot_per_pointer(UINT32 pointer_id, InputMultiTouchType type);

/* Vero se WINQ_DIAG_TOCCO e' accesa nell'ambiente. Esportata perche' anche il
 * filtro sul tipo di puntatore, che sta in winq-window.c, deve poter dire cosa
 * ha scartato: un puntatore rifiutato in silenzio e' indistinguibile da un
 * puntatore che non e' mai arrivato. */
bool winq_diag_tocco(void);

/* La tastiera. Un solo punto d'ingresso, chiamato da winq-window.c per
 * WM_(SYS)KEYDOWN/UP con lo stesso wp/lp del messaggio: vk e' il codice
 * virtuale (wParam), NON usato per la traduzione (dipende dal layout della
 * tastiera dell'host); info e' lParam, da cui si estrae lo scancode
 * posizionale (bit 16-23) e il bit dei tasti estesi (bit 24). Vedi il
 * commento sulla funzione in winq-input.c. */
void winq_key(WPARAM vk, LPARAM info, bool premuto);

/* La rotella. Il delta e' quello di WM_MOUSEWHEEL, con segno e non
 * necessariamente multiplo di 120: l'accumulo sta dentro. */
void winq_rotella(int delta);

#ifdef CONFIG_OPENGL
/* --- winq-present.c: contesto GL e presentazione della texture del guest ---
 *
 * Tutto sotto CONFIG_OPENGL perche' egl-helpers.c/egl-context.c/shader.c e
 * console-gl.c, che winq-present.c riusa, entrano nel binario solo quando
 * l'opengl e' stato trovato (ui/meson.build: opengl_ss). Senza GL winq non ha
 * nessun percorso di disegno e rifiuta di partire, invece di aprire una
 * finestra nera muta: vedi winq_early_init. */

/* Inizializza EGL (ANGLE) e alza display_opengl. Va chiamata da .early_init,
 * prima della realize dei device, perche' virtio-gpu-gl legge display_opengl
 * la' e perche' virglrenderer guarda qemu_egl_display quando decide con quali
 * callback partire (hw/display/virtio-gpu-virgl.c:1445-1458).
 * Ritorna false con la causa gia' riportata. */
bool winq_present_early_init(DisplayGLMode mode);

/* Crea il contesto di presentazione e la superficie EGL su winq.hwnd.
 * Da chiamare dopo winq_create_window. Ritorna false con la causa riportata. */
bool winq_present_init(void);

/* Le quattro operazioni di contesto (include/ui/console.h:279-295), da
 * assegnare a winq.dgc.ops. Sono cio' che rende non-NULL con->gl e quindi
 * evita "assertion failed: (con->gl)" in ui/console.c:985. */
extern const DisplayGLCtxOps winq_gl_ctx_ops;

/* Callback del DisplayChangeListener implementate in winq-present.c. */
void winq_present_gfx_switch(DisplayChangeListener *dcl,
                             DisplaySurface *new_surface);
void winq_present_gfx_update(DisplayChangeListener *dcl,
                             int x, int y, int w, int h);
void winq_present_scanout_texture(DisplayChangeListener *dcl,
                                  uint32_t backing_id,
                                  bool backing_y_0_top,
                                  uint32_t backing_width,
                                  uint32_t backing_height,
                                  uint32_t x, uint32_t y,
                                  uint32_t w, uint32_t h,
                                  void *d3d_tex2d);
void winq_present_scanout_disable(DisplayChangeListener *dcl);
void winq_present_gl_update(DisplayChangeListener *dcl,
                            uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Ridisegna e presenta cio' che c'e' adesso, senza aspettare il guest.
 * Chiamata dal .dpy_refresh quando ci sono aggiornamenti pendenti o quando la
 * finestra e' stata ridimensionata. */
void winq_present_frame(void);
/* Da chiamare su WM_SIZE: azzera il limite di tentativi con cui la presentazione
 * fa convergere la catena di scambio di ANGLE dopo un ridimensionamento. Senza,
 * quel limite si esaurisce una volta per processo invece che una per
 * ridimensionamento. */
void winq_present_nuova_dimensione(void);

/* Vero quando la risoluzione in winq.geom viene dallo scanout del guest e non dal
 * framebuffer di avvio. Vedi il commento sulla funzione in winq-present.c. */
bool winq_present_in_scanout(void);

/* --- il percorso WGL, cioe' il contesto host OpenGL DESKTOP (winq-wgl.c).
 *
 * Esiste perche' misurato: con ANGLE il guest ottiene ES 2.0 e la mediana per frame e'
 * 34 ms; con un contesto desktop -- che e' cio' che -display sdl ottiene via WGL -- il
 * guest ottiene ES 3.1 e la mediana e' 7 ms.
 *
 * WGL e' il DEFAULT; si torna ad ANGLE con WINQ_GL=angle, e il ripiego
 * e' anche automatico se il contesto desktop non si ottiene. */
bool winq_wgl_attivo(void);
void winq_wgl_rinuncia(void);
bool winq_wgl_early_init(void);
bool winq_wgl_init(HWND hwnd);
bool winq_wgl_corrente(void);
void winq_wgl_swap(void);
bool winq_wgl_dimensione(int *w, int *h);
QEMUGLContext winq_wgl_ctx_create(DisplayGLCtx *dgc, QEMUGLParams *params);
void winq_wgl_ctx_destroy(DisplayGLCtx *dgc, QEMUGLContext ctx);
int winq_wgl_make_current(DisplayGLCtx *dgc, QEMUGLContext ctx);
QEMUGLContext winq_wgl_get_current(DisplayGLCtx *dgc);
#endif /* CONFIG_OPENGL */

#endif
