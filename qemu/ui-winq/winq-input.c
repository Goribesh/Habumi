/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-input.c
 * Il tocco: traduce i messaggi WM_POINTER* di Windows negli eventi che Android
 * pretende da uno schermo tattile.
 *
 * La misura che motiva questo file: con il backend SDL arrivavano al guest 864
 * ABS_X e 952 ABS_Y ma ZERO BTN_TOUCH su 1058 eventi EV_KEY, e Android scarta
 * il movimento di un dispositivo assoluto che non dichiari il contatto. Il
 * puntatore non si muoveva.
 *
 * Il difetto NON era in QEMU: hw/input/virtio-input-hid.c:34 mappa gia'
 * [INPUT_BUTTON_TOUCH] = BTN_TOUCH, e virtio_multitouch_init (righe 498-523)
 * dichiara ABS_MT_SLOT / ABS_MT_TRACKING_ID / ABS_MT_POSITION_X / _Y piu'
 * INPUT_PROP_DIRECT. Era SDL che non chiamava mai il percorso del tocco.
 * Tutto cio' che serve e' chiamarlo, e console_handle_touch_event
 * (ui/console.c:600-663) lo fa in una sola chiamata -- e' il percorso che
 * ui/gtk.c:1204 usa da anni.
 *
 * ATTENZIONE alla riga di comando: gli eventi prodotti qui hanno bisogno di un
 * gestore con INPUT_EVENT_MASK_MTT, e l'unico che ce l'ha e'
 * virtio_multitouch_handler (hw/input/virtio-input-hid.c:449-454). Senza
 * "-device virtio-multitouch-pci" qemu_input_find_handler (ui/input.c:101-122)
 * non trova nessuno per gli eventi MTT e li scarta in silenzio, mentre
 * BTN_TOUCH finirebbe su un altro dispositivo: nessun errore, nessun tocco.
 *
 * DAL c'e' ANCHE un "-device virtio-mouse-pci", per la rotella, e
 * anche lui rivendica i BTN. Non ruba il tocco per una ragione precisa: la riga
 * di comando lega tastiera e multitouch alla console con display=gpu0 e lascia
 * il mouse non legato, cosi' qemu_input_find_handler consegna al multitouch
 * cio' che arriva con winq.con e al mouse cio' che arriva con NULL. Il
 * dettaglio completo e' nel commento di winq_rotella, in fondo a questo file.
 * Se qualcuno togliesse quei display=gpu0 dalla riga di comando il tocco
 * morirebbe: e' l'unico modo in cui questa aggiunta puo' fare danno, ed e'
 * evidente subito.
 *
 * Niente GL qui, di proposito: questo file non sa nulla della presentazione, e
 * la sola cosa che condivide con winq-present.c e' la geometria -- la stessa
 * WinqGeom, quindi la stessa formula, quindi disegno e tocco non possono
 * divergere (vedi il commento in winq-coord.h).
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "ui/console.h"
#include "ui/input.h"
#include "winq.h"
#include "winq-rotella.h"

/* Stato per dito, uno per tutta la vita del processo, come touch_slots in
 * ui/gtk.c:135. Non e' "nostro": lo scrive console_handle_touch_event, ed e'
 * l'unica copia autorevole di quali dita siano appoggiate. La compensazione
 * qui sotto lo interroga invece di tenere un contatore proprio, appunto per
 * non poterne divergere. */
static struct touch_slot winq_touch_slots[INPUT_EVENT_SLOTS_MAX];

/* Gli identificativi che Windows assegna ai puntatori non sono contigui e non
 * ripartono da zero, mentre gli slot lo devono essere: console_handle_touch_event
 * usa il numero passato come INDICE nell'array qui sopra e rifiuta tutto cio'
 * che sia >= INPUT_EVENT_SLOTS_MAX.
 *
 * "in_uso" e' separato dall'identificativo, e non si usa lo zero come valore
 * di slot libero: Windows non documenta 0 come identificativo impossibile, e
 * se mai lo assegnasse lo slot sembrerebbe libero mentre un dito ci sta
 * sopra. Il sintomo sarebbe un dito che non si stacca piu'. */
typedef struct {
    UINT32 id;
    bool in_uso;
} WinqDito;
static WinqDito winq_dita[INPUT_EVENT_SLOTS_MAX];

/* Abbiamo emesso BTN_TOUCH a 1 e non ancora il corrispondente 0: vedi la
 * compensazione in fondo a winq_handle_pointer_event. */
static bool winq_btn_touch_giu;

/* Diagnostica opzionale del tocco, spenta se la variabile non c'e'. Serve alla
 * verifica della corrispondenza spaziale: getevent dentro il guest mostra le
 * coordinate ARRIVATE, non quelle di partenza nella finestra, e senza le due
 * meta' non si puo' dire se uno scostamento venga dalla mappatura o da Android.
 *
 * Il nome NON e' WINQ_DIAG: quella e' gia' presa dalla diagnostica del ponte
 * winq-emu (guest/README.md:108) e riusarla mescolerebbe due registri. */
bool winq_diag_tocco(void)
{
    static int stato = -1;

    if (stato < 0) {
        const char *v = getenv("WINQ_DIAG_TOCCO");
        stato = (v && *v && *v != '0') ? 1 : 0;
    }
    return stato == 1;
}

/* OBBLIGATORIA, e non e' una formalita': per gli slot lo zero non e' lo stato
 * di riposo. console_handle_touch_event (ui/console.c:628-639) scorre TUTTI gli
 * slot a ogni evento e salta solo quelli con tracking_id == -1. Con l'array
 * semplicemente azzerato dal caricatore ogni slot ha tracking_id 0, che per
 * quella funzione vuol dire "active finger, id 0": al primo tocco
 * emetterebbe posizione e BTN_TOUCH per tutte e dieci le dita, tutte in (0,0).
 * Sintomo: dieci tocchi fantasma nell'angolo dello schermo del guest a ogni
 * evento, e il tocco vero perso nel mezzo.
 *
 * ui/gtk.c:2353-2356 fa esattamente questo giro, per esattamente questa
 * ragione. Il codice pianificato per questo task non ce l'aveva. */
void winq_input_init(void)
{
    int i;

    for (i = 0; i < INPUT_EVENT_SLOTS_MAX; i++) {
        winq_touch_slots[i].tracking_id = -1;
        winq_dita[i].in_uso = false;
    }
    winq_btn_touch_giu = false;
}

uint64_t winq_slot_per_pointer(UINT32 pointer_id, InputMultiTouchType type)
{
    int i;

    for (i = 0; i < INPUT_EVENT_SLOTS_MAX; i++) {
        if (winq_dita[i].in_uso && winq_dita[i].id == pointer_id) {
            if (type == INPUT_MULTI_TOUCH_TYPE_END) {
                winq_dita[i].in_uso = false;
            }
            return i;
        }
    }

    if (type != INPUT_MULTI_TOUCH_TYPE_BEGIN) {
        /* Aggiornamento o rilascio di un dito che non abbiamo mai visto
         * scendere: succede se la finestra nasce con un dito gia' appoggiato,
         * o dopo un WM_POINTERCAPTURECHANGED che ha gia' chiuso il tocco.
         * Il chiamante scarta. */
        return INPUT_EVENT_SLOTS_MAX;
    }

    for (i = 0; i < INPUT_EVENT_SLOTS_MAX; i++) {
        if (!winq_dita[i].in_uso) {
            winq_dita[i].id = pointer_id;
            winq_dita[i].in_uso = true;
            return i;
        }
    }
    /* Piu' dita di quante il dispositivo del guest ne dichiari: si ignora
     * l'undicesima invece di rubare lo slot a una delle dieci gia' giu'. */
    return INPUT_EVENT_SLOTS_MAX;
}

void winq_handle_pointer_event(uint64_t slot, int wx, int wy,
                               InputMultiTouchType type)
{
    Error *err = NULL;
    bool qualcuno_giu = false;
    int gx = 0, gy = 0;
    int i;

    if (slot >= INPUT_EVENT_SLOTS_MAX) {
        return;
    }

    if (type == INPUT_MULTI_TOUCH_TYPE_END) {
        /* Al rilascio la posizione non porta informazione: il ramo END di
         * console_handle_touch_event emette solo tracking_id = -1. Quella
         * funzione pero' scrive slot->x/y PRIMA di guardare il tipo
         * (ui/console.c:621-623), quindi passarle (0,0) lascerebbe nello slot
         * una coordinata mai toccata. Si ripassa l'ultima posizione nota. */
        gx = winq_touch_slots[slot].x;
        gy = winq_touch_slots[slot].y;
    } else if (!winq_win_to_guest(&winq.geom, wx, wy, &gx, &gy)) {
        /* Un tocco nelle bande nere si SCARTA, non si satura al bordo:
         * saturare produrrebbe tocchi fantasma sul lato dello schermo, che
         * nell'interfaccia di Android aprono pannelli laterali senza che
         * l'utente li abbia chiesti.
         *
         * Se un dito gia' giu' esce dall'immagine trascinando, i suoi
         * aggiornamenti cadono qui e il dito resta dove era: non si stacca da
         * solo, perche' il dito vero e' ancora sul vetro. Si staccara' col suo
         * WM_POINTERUP, che passa per il ramo END qui sopra e non da questa
         * parte. */
        if (winq_diag_tocco()) {
            info_report("winq touch: (%d,%d) outside the image, dropped "
                        "(window %dx%d, guest %dx%d)", wx, wy,
                        winq.geom.win_w, winq.geom.win_h,
                        winq.geom.guest_w, winq.geom.guest_h);
        }
        return;
    }

    /* UN DITO NATO NELLE BANDE NERE NON E' PERSO PER SEMPRE.
     *
     * Trovato dalla review finale, e il ramo sopra da solo non bastava.
     * console_handle_touch_event assegna il tracking_id SOLO sul BEGIN
     * (ui/console.c:624-626). Scartato il BEGIN perche' cadeva fuori
     * dall'immagine, lo slot resta legato al pointer_id ma con tracking_id -1, e
     * OGNI aggiornamento successivo viene saltato da quella funzione anche
     * quando il dito e' entrato nell'immagine trascinando.
     *
     * Sintomo: uno scorrimento che comincia dal bordo non fa assolutamente
     * nulla, in silenzio e senza un errore. Su una finestra con bande nere larghe
     * -- cioe' ogni volta che le proporzioni non coincidono -- e' un gesto
     * normale, non un caso limite.
     *
     * Il rimedio non e' saturare al bordo: la ragione scritta sopra resta valida,
     * un tocco saturato aprirebbe i pannelli laterali di Android da solo. Si
     * PROMUOVE invece il primo aggiornamento che riesce a entrare: per Android
     * quel dito nasce dove ha varcato il bordo dell'immagine, che e' esattamente
     * cio' che l'utente vede.
     *
     * La condizione guarda il tracking_id e non un nostro contatore, per la stessa
     * ragione della compensazione di BTN_TOUCH piu' sotto: l'array degli slot e'
     * l'unica fonte autorevole, ed e' quella che la funzione a monte legge. */
    if (type == INPUT_MULTI_TOUCH_TYPE_UPDATE &&
        winq_touch_slots[slot].tracking_id == -1) {
        if (winq_diag_tocco()) {
            info_report("winq touch: slot %d enters the image after a dropped "
                        "BEGIN, promoted to BEGIN", (int)slot);
        }
        type = INPUT_MULTI_TOUCH_TYPE_BEGIN;
    }

    if (winq_diag_tocco()) {
        info_report("winq touch: slot %d type %d window (%d,%d) -> guest "
                    "(%d,%d) [window %dx%d, guest %dx%d]",
                    (int)slot, (int)type, wx, wy, gx, gy,
                    winq.geom.win_w, winq.geom.win_h,
                    winq.geom.guest_w, winq.geom.guest_h);
    }

    console_handle_touch_event(winq.con, winq_touch_slots, slot,
                               winq.geom.guest_w, winq.geom.guest_h,
                               gx, gy, type, &err);
    if (err) {
        /* Non puo' scattare: l'unico error_setg della funzione e' su
         * num_slot >= INPUT_EVENT_SLOTS_MAX, gia' escluso sopra. Si riporta
         * comunque, perche' inghiottirlo renderebbe muto un cambio di quella
         * funzione a monte. */
        error_report_err(err);
        return;
    }

    /* COMPENSAZIONE DEL RILASCIO DI BTN_TOUCH.
     *
     * Misurato leggendo ui/console.c:637-658: console_handle_touch_event emette
     * qemu_input_queue_btn(INPUT_BUTTON_TOUCH, true) a OGNI aggiornamento di un
     * dito attivo, ma il ramo END emette solo tracking_id = -1 e non riporta mai
     * il pulsante a false. Nel protocollo multitouch Linux di tipo B BTN_TOUCH
     * deve scendere quando l'ultimo dito si stacca.
     *
     * Sintomo previsto senza compensazione: il primo tocco funziona, e poi
     * Android crede che il dito sia rimasto premuto per sempre -- un
     * trascinamento che non finisce mai. Nessun errore, nessun messaggio: fra i
     * guasti piu' difficili da diagnosticare, ed e' per questo che la verifica
     * di questo task conta le DUE transizioni separatamente e non le occorrenze
     * di BTN_TOUCH.
     *
     * ui/gtk.c usa lo stesso percorso e ha quindi la stessa asimmetria: e' a
     * monte in QEMU, non una nostra svista.
     *
     * La condizione non e' "type == END" ma "nessuno slot ha piu' un
     * tracking_id valido", letto dall'array che console_handle_touch_event
     * stessa mantiene. Cosi':
     *  - con piu' dita, il rilascio della prima non spegne il contatto mentre
     *    le altre sono ancora giu';
     *  - non esiste un contatore nostro che possa divergere da quello di QEMU
     *    (per esempio quando un BEGIN e' stato scartato perche' cadeva nelle
     *    bande nere, e il suo END arriva comunque);
     *  - non si emette un rilascio che non sia stato preceduto da una
     *    pressione. */
    for (i = 0; i < INPUT_EVENT_SLOTS_MAX; i++) {
        if (winq_touch_slots[i].tracking_id != -1) {
            qualcuno_giu = true;
            break;
        }
    }

    if (qualcuno_giu) {
        /* Almeno un dito attivo: la chiamata qui sopra ha appena emesso
         * BTN_TOUCH a 1, e prima o poi ne dovremo il 0. */
        winq_btn_touch_giu = true;
    } else if (winq_btn_touch_giu) {
        winq_btn_touch_giu = false;
        qemu_input_queue_btn(winq.con, INPUT_BUTTON_TOUCH, false);
        qemu_input_event_sync();
        if (winq_diag_tocco()) {
            info_report("winq touch: last finger up, BTN_TOUCH to 0 "
                        "(compensazione dell'asimmetria di "
                        "console_handle_touch_event)");
        }
    }
}

/* --- La tastiera --------------------------------------------------------
 *
 * Molto piu' corto del tocco: nessuno slot, nessuna compensazione, nessuno
 * stato che sopravviva a una singola pressione. Un tasto arriva, si traduce,
 * si spedisce.
 *
 * ATTENZIONE ALLA TABELLA: si usa atset1, NON win32. In keycodemapdb il nome
 * "win32" indica i CODICI VIRTUALI (VK_*), non gli scancode; gli scancode AT
 * set-1 stanno in atset1. Il piano diceva win32 e si sbagliava.
 *
 * MISURATO dall'utente sulla tastiera fisica: premendo M arrivava 2. Non e' un
 * caso, e' aritmetica -- lo scancode di M e' 0x32, e 0x32 e' VK_2:
 *
 *     build/ui/input-keymap-win32-to-qcode.c.inc
 *       [0x32] = Q_KEY_CODE_2    // win32:50 (VK_2)
 *       [0x4d] = Q_KEY_CODE_M    // win32:77 (VK_M)
 *     build/ui/input-keymap-atset1-to-qcode.c.inc
 *       [0x32] = Q_KEY_CODE_M    // atset1:50
 *
 * Indicizzare la tabella dei VK con uno scancode da' quindi un tasto
 * plausibile e sbagliato, che e' il modo peggiore di sbagliare: nessun errore,
 * nessun tasto morto, solo lettere che non corrispondono.
 *
 * L'involucro e' modellato su qemu_input_linux_to_qcode
 * (ui/input-keymap.c:25-31), l'unica funzione gemella con la stessa forma
 * tabella+lunghezza+controllo del limite. */
static int winq_scancode_to_qcode(int scancode)
{
    if (scancode < 0 || (guint)scancode >= qemu_input_map_atset1_to_qcode_len) {
        /* Fuori limite vuol dire "tasto che QEMU non conosce", non un errore
         * nostro. La tabella atset1 ha 57470 voci perche' indicizza anche i
         * codici estesi 0xe0XX, ma sparsi: la maggior parte degli indici e'
         * zero, cioe' Q_KEY_CODE_UNMAPPED. */
        return Q_KEY_CODE_UNMAPPED;
    }
    return qemu_input_map_atset1_to_qcode[scancode];
}

/* Un solo punto d'ingresso per evento di tastiera. Chiamata da
 * winq-window.c per WM_KEYDOWN/WM_KEYUP e per i loro gemelli WM_SYSKEYDOWN/
 * WM_SYSKEYUP (questi ultimi arrivano quando Alt e' gia' giu', per esempio
 * Alt+Tab o Alt+F4: senza intercettarli anche loro, tenere Alt premuto
 * mentre si preme un'altra cosa non arriverebbe mai al guest).
 *
 * vk (il codice virtuale, wParam) e' ricevuto ma DELIBERATAMENTE ignorato:
 * dipende dal layout di tastiera attivo sull'host. Con un layout italiano,
 * francese o tedesco lo stesso tasto fisico produce wParam diversi da quelli
 * di un layout US, e usarlo manderebbe al guest il tasto sbagliato. Lo
 * scancode invece e' POSIZIONALE -- la stessa riga/colonna della tastiera
 * fisica produce sempre lo stesso scancode qualunque sia il layout -- ed e'
 * per questo che la tabella di QEMU e' indicizzata per scancode e non per
 * codice virtuale. */
void winq_key(WPARAM vk, LPARAM info, bool premuto)
{
    int scancode = (info >> 16) & 0xff;
    int qcode;

    (void)vk;   /* vedi il commento sopra: si usa lo scancode, non vk */

    /* Il bit 24 di lParam distingue i tasti ESTESI. Windows assegna lo
     * STESSO scancode a 7 bit a coppie come le frecce e i tasti 2/4/6/8/7/9/
     * 1/3 del tastierino numerico, o Canc e il punto del tastierino: si
     * distinguono solo da questo bit.
     *
     * Il prefisso e' 0xe000, NON il bit 0x80. Verificato nella tabella
     * generata: atset1 codifica gli estesi come 0xe0 seguito dallo scancode,
     * ed esiste ZERO voci fra 0x80 e 0xff --
     *     [0xe048] = Q_KEY_CODE_UP        (freccia su)
     *     [0xe053] = Q_KEY_CODE_DELETE    (Canc)
     *     [0xe01c] = Q_KEY_CODE_KP_ENTER  (Invio del tastierino)
     * quindi con 0x80 ogni tasto esteso cadeva su una voce vuota e veniva
     * scartato in silenzio: le frecce non arrivavano affatto al guest.
     *
     * Ignorare il bit del tutto manderebbe invece il tasto del tastierino
     * invece della freccia -- silenziosamente, senza nessun errore. */
    if (info & (1 << 24)) {
        scancode |= 0xe000;
    }

    qcode = winq_scancode_to_qcode(scancode);
    if (qcode == Q_KEY_CODE_UNMAPPED) {
        return;
    }

    /* Nessuna qemu_input_event_sync() esplicita qui, a differenza del tocco
     * poco sopra: qemu_input_event_send_key_qcode chiama
     * qemu_input_event_send_key (ui/input.c), che sincronizza gia' da sola
     * quando la coda differita del tasto (kbd_queue) e' vuota -- il caso
     * normale, perche' winq non passa mai da quella coda. Aggiungerne una
     * seconda qui non romperebbe nulla ma sarebbe ridondante e fuorviante
     * per chi legge, come se servisse. */
    qemu_input_event_send_key_qcode(winq.con, qcode, premuto);
}

/* --- La rotella ---------------------------------------------------------
 *
 * PERCHE' IL MITTENTE E' NULL, e non winq.con come per il tocco. E' la chiave
 * di tutto il meccanismo, quindi va spiegata qui e non altrove.
 *
 * In QEMU la rotella non e' un evento a se': e' un PULSANTE
 * (INPUT_BUTTON_WHEEL_UP/DOWN), e hw/input/virtio-input-hid.c:111-117 lo
 * traduce in EV_REL/REL_WHEEL per qualunque dispositivo virtio-input lo
 * riceva. Chi lo riceve lo decide qemu_input_find_handler (ui/input.c:101):
 * primo giro sui gestori legati alla console del mittente, secondo giro sui
 * non legati, e vince il PRIMO la cui maschera contiene il tipo dell'evento.
 * Sia virtio_mouse_handler (BTN|REL) sia virtio_multitouch_handler (BTN|MTT)
 * rivendicano i BTN.
 *
 * La riga di comando (app/guscio/vm.c) lega tastiera e multitouch alla
 * console con display=gpu0 e lascia il mouse NON legato. Quindi:
 *   - il tocco, mandato con winq.con, viene preso al primo giro dal multitouch;
 *   - la rotella, mandata con NULL, salta tutto il primo giro (con con == NULL
 *     la condizione di scarto e' sempre vera) e al secondo trova il solo
 *     gestore non legato: il mouse.
 * Mettere winq.con qui manderebbe la rotella al multitouch, che non dichiara
 * EV_REL: nessun errore, nessuno scorrimento. Il fallimento sarebbe SILENZIOSO.
 *
 * La pressione e il rilascio si mandano entrambi, come fanno gtk e sdl, pur
 * sapendo che virtio-input-hid.c:113 traduce solo la pressione (&& btn->down):
 * il rilascio non produce nulla e non fa danno, e mandarlo tiene questo codice
 * uguale al resto di QEMU invece di essere un caso speciale da spiegare. */
void winq_rotella(int delta)
{
    /* Un solo mouse, un solo accumulo: static va bene e non serve altro stato. */
    static int resto;
    int scatti = winq_rotella_scatti(delta, &resto);
    InputButton pulsante;
    int i;

    if (scatti == 0) {
        return;
    }
    pulsante = scatti > 0 ? INPUT_BUTTON_WHEEL_UP : INPUT_BUTTON_WHEEL_DOWN;
    if (scatti < 0) {
        scatti = -scatti;
    }
    for (i = 0; i < scatti; i++) {
        qemu_input_queue_btn(NULL, pulsante, true);
        qemu_input_queue_btn(NULL, pulsante, false);
    }
    qemu_input_event_sync();
}
