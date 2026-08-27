/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* vm.c -- il ciclo di vita di QEMU: argomenti, avvio, riprova, spegnimento.
 *
 * PERCHE' LO SPEGNIMENTO STA QUI e non in un file suo: e' una fase del ciclo di
 * vita, cambia insieme all'avvio e alla riprova, e separarlo vorrebbe dire
 * tenere in pari due file per una cosa sola. */
#include <stdio.h>
#include <stdlib.h>    /* getenv e strtol, per WINQ_SND_BUFFER_US */
#include <string.h>
#include <windows.h>
#include "guscio.h"

/* Gli argomenti come UNA sola stringa, e le virgolette le decide questo file.
 *
 * IL DIFETTO CHE QUESTA SCELTA EVITA, e lo script che sostituiamo l'ha pagato:
 * passando gli argomenti come array, Start-Process li unisce con spazi senza
 * citare quelli che ne contengono, e QEMU riceveva "printk.devkmsg=on" come
 * nome di file. Citando a mano dentro l'elemento e' peggio: le virgolette
 * finiscono nel valore e il kernel legge
 *     Kernel command line: "console=ttyAMA0 audit=0"
 * cioe' non configura la seriale e ignora tutti gli androidboot.*. Il sintomo e'
 * un guest che sembra inchiodato e una finestra su "Display output is not
 * active" -- nessuna delle due righe nomina il quoting. */
/* I vCPU DI QUESTO TENTATIVO, che non sono per forza quelli configurati.
 *
 * PERCHE'. I tre tentativi esistevano per rigiocare una corsa non
 * deterministica, e rigiocavano la stessa identica configurazione. Se il guasto
 * e' invece DETERMINISTICO, tre tentativi identici sono tre volte lo stesso
 * fallimento -- e sono cinquanta secondi buttati prima di dire all'utente una
 * cosa che non lo aiuta.
 *
 * DUE MACCHINE lo hanno mostrato: su Surface Pro 12" (Snapdragon X Plus a 8
 * core) con vcpu=6 la seriale produce ZERO righe a ogni tentativo, mentre con
 * vcpu=1 il kernel parte. Zero righe vuol dire che il guest non esegue
 * nemmeno la prima istruzione: non e' la bring-up SMP del kernel, e' WHPX che
 * non riesce a creare le vCPU in piu'.
 *
 * QUINDI IL RITENTATIVO DEGRADA: se un tentativo non ha prodotto UNA SOLA
 * riga, il successivo dimezza i vCPU. Con il default di 6 la sequenza e'
 * 6 -> 3 -> 1, e su quelle due macchine il terzo tentativo sarebbe partito
 * senza che nessuno leggesse un registro.
 *
 * Si dimezza SOLO a zero righe, non a "poche": poche righe vuol dire che il
 * guest esegue, e allora il numero di vCPU non e' il sospetto -- lo tratta il
 * guardiano del progresso qui sopra. */
static int vm_vcpu_ora;

/* PERCHE' IL KERNEL STA IN guest/images/ E LE IMMAGINI IN
 * guest/images/android/: sono artefatti di due catene di costruzione
 * diverse. Il kernel lo scrive build-guest-kernel.sh direttamente in
 * guest/images/. Le quattro immagini del guest (initrd, system, vendor,
 * data) le scrive build-all.ps1 sotto guest/images/android/. Non sono la
 * stessa cartella per caso: appiattirle qui senza toccare le due build
 * reintroduce esattamente il difetto che questo file corregge. */
int vm_argomenti(const Config *c, char *buf, int max)
{
    int n;
    /* IL FRAMMENTO DELL'AUDIO, costruito a parte perche' la lunghezza del
     * buffer host ora si puo' cambiare da fuori PER MISURARE. Il default resta
     * 30000: e' il valore spedito e misurato, e non cambia senza una misura che
     * lo giustifichi.
     *
     * PERCHE' SI RIAPRE UNA COSA GIA' SCARTATA. Un buffer da 200 ms con timer a
     * 5 ms era stato provato e scartato (63% di campioni a zero, peggio del
     * doppio) con la spiegazione "un buffer host GRANDE affama il guest" -- vedi
     * la tabella nel commento qui sotto. Quella prova cambiava DUE variabili in
     * una volta, e soprattutto misurava un guest il cui driver virtio-snd si
     * comportava in modo diverso: il driver e' stato corretto per
     * chiamare snd_pcm_period_elapsed una volta per periodo con una posizione
     * riportata che avanza di un periodo alla volta. Il meccanismo su cui
     * poggiava il rifiuto non c'e' piu', quindi il rifiuto va rimisurato.
     *
     * Il motivo per riprovare, con il numero: la coda del ritardo di scrittura
     * verso l'HAL misurata e' 61,6 ms (media 0,027, std 4,68)
     * contro un buffer di 30 ms. Un ritardo piu' lungo del buffer produce
     * silenzio, e all'altoparlante si contano 12 silenzi in 30 s che l'utente
     * SENTE in cuffia.
     *
     * Il timer resta a 2500 us: si cambia una variabile per volta. */
    static char audio_frammento[192];
    long buffer_us = 30000;
    const char *immagine;
    const char *dati;

    /* La coppia di immagini dipende dalla variante, e viene dalla tabella di
     * varianti.c invece di restare cablata qui: vedi var_percorsi in
     * varianti.h. vendor.img resta fisso sotto, uguale per entrambe le
     * varianti, perche' tutte le personalizzazioni del progetto viaggiano
     * nell'initramfs. La riga di registro compensa i nomi asimmetrici -- la
     * vanilla e' system.img senza suffisso -- ed e' l'unico modo di sapere a
     * colpo d'occhio quale variante e quali file si stanno avviando. */
    var_percorsi(c->variante, &immagine, &dati);
    registro_riga(REG_GUSCIO, "variant %s: %s + %s",
                  var_testo(c->variante), immagine, dati);

    {
        const char *v = getenv("WINQ_SND_BUFFER_US");

        if (v && *v) {
            long q = strtol(v, NULL, 10);

            /* Fuori intervallo si IGNORA e si tiene il default, come fa la
             * configurazione: un valore assurdo non deve impedire di partire. */
            if (q >= 5000 && q <= 500000) {
                buffer_us = q;
            }
        }
    }

    if (c->audio) {
        snprintf(audio_frammento, sizeof(audio_frammento),
                 " -audiodev dsound,id=a0,out.fixed-settings=off"
                 ",out.buffer-length=%ld,timer-period=2500"
                 " -device virtio-sound-pci,audiodev=a0", buffer_us);
    } else {
        audio_frammento[0] = '\0';
    }

    n = snprintf(buf, (size_t)max,
        /* -smp NON viene da c->vcpu ma da vm_vcpu_ora: i tentativi successivi
         * lo dimezzano quando il precedente non ha prodotto una sola riga.
         * Vedi il commento su vm_vcpu_ora. */
        "-M virt -accel whpx -cpu host -m %d -smp %d "
        "-kernel guest/images/kernel-guest-arm64 "
        "-initrd guest/images/android/initramfs.img "
        "-append \"console=ttyAMA0 printk.devkmsg=on "
        "androidboot.hardware=waydroid androidboot.selinux=permissive audit=0\" "
        "-drive file=%s,format=raw,if=virtio,readonly=on "
        "-drive file=guest/images/android/vendor.img,format=raw,if=virtio,readonly=on "
        "-drive file=%s,format=raw,if=virtio,"
        "cache=writeback,aio=threads "
        /* id=gpu0 e i due display=gpu0 non sono cosmetici: legano tastiera e
         * multitouch a QUESTA console, e quel legame e' l'unica cosa che
         * impedisce al mouse qui sotto di rubare gli eventi del tocco. Il
         * mouse resta volutamente SENZA display: e' cosi' che riceve la
         * rotella, che winq manda con mittente NULL. Il meccanismo per intero
         * e' nel commento di winq_rotella, in ui/winq-input.c. */
        "-device virtio-gpu-gl-pci,venus=on,blob=on,hostmem=256M,"
        "xres=%d,yres=%d,id=gpu0 "
        "-device virtio-keyboard-pci,display=gpu0 "
        "-device virtio-multitouch-pci,display=gpu0 "
        "-device virtio-mouse-pci "
        "-netdev user,id=n0,hostfwd=tcp:127.0.0.1:%d-:5555 "
        "-device virtio-net-pci,netdev=n0 "
        "-display winq,gl=on "
        "-serial file:guest/logs/sessione-viva.log "
        "-monitor none%s",
        c->memoria, vm_vcpu_ora > 0 ? vm_vcpu_ora : c->vcpu,
        immagine, dati, c->larghezza, c->altezza,
        c->porta_adb,
        /* LE TRE OPZIONI DELL'AUDIO SONO TUTTE MISURATE, e la ragione di
         * fondo e' una sola: IL GUEST APRE IL PCM CON UN BUFFER DI 21 ms
         * (period_size 256 frame, buffer_size 1024, letti da
         * /proc/asound/card0/pcm0p/sub0/hw_params). Dentro quella finestra
         * l'host deve restituire i completamenti, altrimenti il guest resta
         * bloccato con hw_ptr fermo e DirectSound rilegge il frammento che
         * ha -- ed e' il "suono sempre uguale che si ripete" che l'utente ha
         * sentito, non uno stutter.
         *
         *   out.fixed-settings=off  senza, QEMU apre a 44100 fissi e
         *                           ricampiona i 48000 del guest a ogni frame
         *   out.buffer-length=30000 buffer host di 30 ms, vicino ai 21 del
         *                           guest: PICCOLO, non grande
         *   timer-period=2500       si guarda la coda ogni 2,5 ms invece di 10
         *
         * Misura, campioni a zero sul misuratore di Windows con un tono
         * continuo (100 campioni, uno ogni 100 ms):
         *   com'era                                      37%  picco 0,0249
         *   solo fixed-settings=off                      14%        0,0360
         *   fixed-settings=off + buffer 30 ms + 2,5 ms    0%        0,0425
         * Provate e SCARTATE con la stessa misura: buffer da 200 ms con timer
         * a 5 ms (63%, peggio del doppio -- un buffer host GRANDE affama il
         * guest) e il backend sdl (42%). Tabella in app/GUSCIO.md. */
        audio_frammento);

    if (n < 0 || n >= max) {
        return 0;
    }
    return n;
}

/* UN SOLO dispositivo di puntamento, di proposito: con due Android li fonde in
 * un'unica voce logica, perche' i device virtio non portano identificatori
 * distinti, e si ritrova due mapper sullo stesso dispositivo. Il sintomo
 * misurato e' un cursore che si muove e clic che cadono al centro.
 *
 * E multitouch e non mouse, perche' il display e' winq: virtio-mouse dichiara
 * MASK_BTN ma non MASK_MTT, e qemu_input_find_handler scarta IN SILENZIO gli
 * eventi senza destinatario (ui/input.c:101-122). Col dispositivo sbagliato al
 * guest arrivano i BTN_TOUCH ma NON le posizioni: Android sa che un dito e'
 * appoggiato e non sa dove. Misurato: 398 eventi consegnati a
 * virtio-mouse-device, zero al multitouch che non c'era. */

/* Solo i file che NON dipendono dalla variante: niente immagine di sistema ne'
 * di dati qui. Quelle le sceglie var_percorsi, in vm_file_necessario qui
 * sotto, in base alla variante che il chiamante passa.
 *
 * PRIMA di questo selettore le due immagini stavano cablate in questo stesso
 * array, sempre sulla vanilla (system.img, data.img), perche' era l'unica
 * variante che esistesse. Con un secondo Android quella cablatura diverge da
 * cio' che vm_argomenti manda davvero a QEMU non appena la variante attiva e'
 * gapps: vm_verifica_file controllerebbe la coppia sbagliata, cercando la
 * vanilla anche quando e' la gapps a dover partire (e viceversa) -- vedi la
 * revisione che ha trovato il difetto, riportata nel rapporto del task. */
static const char *vm_necessari_fissi[] = {
    "guest/images/kernel-guest-arm64",
    "guest/images/android/initramfs.img",
    "guest/images/android/vendor.img",
    "runtime/bin/qemu-nostro.exe",
};

/* UNICA fonte di verita' per l'elenco dei file necessari: vm_verifica_file
 * scorre l'elenco chiamando questa funzione invece di leggere un array
 * direttamente, e le prove fanno lo stesso per controllare che l'elenco
 * concordi con vm_argomenti. Gli indici oltre vm_necessari_fissi sono le DUE
 * immagini della variante data -- sistema e dati, in quest'ordine -- prese da
 * var_percorsi: la STESSA funzione che vm_argomenti chiama per scegliere cosa
 * passare a QEMU. E' questo che tiene i due elenchi legati: non possono piu'
 * divergere come due copie cablate separatamente potrebbero. */
const char *vm_file_necessario(VarNome variante, int indice)
{
    const int n_fissi = (int)(sizeof(vm_necessari_fissi) /
                              sizeof(vm_necessari_fissi[0]));

    if (indice < 0) {
        return NULL;
    }
    if (indice < n_fissi) {
        return vm_necessari_fissi[indice];
    }
    if (indice == n_fissi || indice == n_fissi + 1) {
        const char *immagine;
        const char *dati;

        var_percorsi(variante, &immagine, &dati);
        return (indice == n_fissi) ? immagine : dati;
    }
    return NULL;
}

bool vm_verifica_file(const Config *c, char *mancante, int max)
{
    int i;
    const char *percorso;

    for (i = 0; (percorso = vm_file_necessario(c->variante, i)) != NULL; i++) {
        if (GetFileAttributesA(percorso) == INVALID_FILE_ATTRIBUTES) {
            snprintf(mancante, (size_t)max, "%s", percorso);
            return false;
        }
    }
    /* adb.exe sta accanto all'eseguibile, non nella cartella del progetto.
     * adb_percorso e' la STESSA funzione che usa adb.c per collegarsi a adb:
     * prima qui c'era una seconda copia della stessa logica, SENZA il ramo
     * che azzera il buffer quando GetModuleFileNameA non restituisce un
     * percorso con backslash. Le due copie erano gia' divergenti: due
     * costruzioni dello stesso percorso sono due cose da tenere in pari, e
     * qui non serve tenerne due. */
    {
        char adb[MAX_PATH];

        adb_percorso(adb, sizeof(adb));
        if (GetFileAttributesA(adb) == INVALID_FILE_ATTRIBUTES) {
            snprintf(mancante, (size_t)max, "%s", adb);
            return false;
        }
    }
    return true;
}

/* --- il ciclo di vita ---------------------------------------------------
 *
 * PERCHE' UNA MACCHINA A STATI CHIAMATA DA UN TIMER, e non una sequenza con
 * attese. L'avvio dura cento secondi: una sequenza bloccante lascerebbe la
 * finestra congelata per tutto quel tempo, che e' esattamente il difetto che il
 * guscio esiste per correggere -- oggi non si vede nulla e non si distingue un
 * avvio lento da uno rotto. */

static const Config *vm_c;
static HANDLE vm_processo;
static HANDLE vm_stderr_lettura;
static HANDLE vm_thread_stderr;
static VmStato vm_s;
static DWORD vm_ora_stato;      /* GetTickCount64 troncato: bastano i ms */
static int vm_tentativo;
/* Quanti tentativi sono finiti perche' QEMU e' USCITO SUBITO, invece che
 * perche' il kernel non dava segni di vita. Sono due guasti diversi e il
 * verdetto finale li confondeva: vedi VM_AVVIA piu' sotto. */
static int vm_usciti_subito;
/* Quante righe della seriale sono state SOPPRESSE perche' ripetizioni della
 * ricerca dell'HAL OMX. Vedi vm_riga_omx piu' sotto: si contano per poterlo
 * dire, invece di far sparire righe in silenzio. */
static unsigned long vm_omx_soppresse;
static unsigned long vm_omx_dette;

static FILE *vm_seriale;
static long vm_seriale_pos;
static DWORD vm_ora_spegni;
static DWORD vm_ora_ultima_adb;  /* ultima chiamata ad adb_pronto: vedi il freno in VM_ATTESA_ANDROID */
/* Quando e' arrivata l'ultima riga di seriale. E' cio' che distingue un kernel
 * lento da uno fermo: vedi il commento sulle soglie qui sotto. */
static DWORD vm_ora_ultima_riga;
/* SEDICI SECONDI SONO IL MINIMO DI GRAZIA, non piu' il verdetto.
 *
 * Prima il guardiano guardava un CONTEGGIO a scadenza fissa: a 16 secondi, meno
 * di 200 righe voleva dire "inchiodato". Il commento accanto lo giustificava
 * cosi': un avvio sano supera le trecento righe entro dieci secondi, uno
 * inchiodato resta sotto le cinquanta. Fra i due c'era un caso che nessuno
 * aveva previsto, ed e' arrivato dal primo rapporto su hardware non nostro:
 * una macchina semplicemente PIU' LENTA. Un Surface Pro 12" con Snapdragon
 * X Plus faceva 119 righe in 16 secondi -- e continuava a salire, arrivando
 * ogni tentativo piu' avanti del precedente. Il kernel stava benissimo:
 * eravamo noi a ucciderlo, e a dichiarare per giunta una corsa sui vCPU che
 * non c'entrava niente.
 *
 * IL CRITERIO GIUSTO E' IL PROGRESSO, NON IL CONTEGGIO. Finche' le righe
 * arrivano il kernel e' vivo, e quanto vada piano non e' affar nostro; e'
 * inchiodato quando la seriale TACE. Da qui le tre soglie: un minimo di
 * grazia prima di giudicare, un silenzio che definisce il guasto, e un tetto
 * perche' un avvio vivo ma inconcludente non aspetti per sempre.
 *
 * Il tetto e' generoso di proposito: chi ha una macchina lenta preferisce
 * aspettare due minuti che vedersi dire una bugia in sedici secondi. */
/* I TRE NUMERI, E PERCHE' NON SONO PIU' QUELLI DI PRIMA (issue #1, tre Surface
 * Pro 12" con Snapdragon X Plus). Erano 16000, 8000 e 120000, tarati su questa
 * macchina, dove la prima riga del kernel arriva in 0,2 s. Su quelle tre arriva
 * in QUINDICI SECONDI E MEZZO, e i due numeri piccoli cadevano esattamente
 * sopra il comportamento normale di quelle macchine:
 *
 *   MISURATO sui loro registri, tempo dal lancio di QEMU alla prima riga:
 *     roklipni  tentativo 2   15,6 s   <- sopravvissuto per 400 ms
 *     easysynth tentativo 1   16,0 s   <- esattamente sulla scadenza
 *   e ogni tentativo che il guscio ha dichiarato "0 serial lines" e' stato
 *   ucciso allo scadere preciso dei 16 s, PRIMA che il kernel potesse parlare.
 *
 * Il guasto "non deterministico" era questo: una corsa fra il primo printk del
 * kernel e il nostro cronometro. A volte 15,6 e passava, a volte 16,1 e
 * moriva -- e il verdetto incolpava una corsa sui vCPU secondari sotto WHPX che
 * non e' mai esistita. PROVA che non esisteva: su quelle macchine il kernel
 * parte con SEI vCPU, quando lo si lascia parlare.
 *
 *   MISURATO, il silenzio dentro un avvio SANO su quelle macchine:
 *     buchi fra due righe consecutive, avvio che arriva alla userspace di
 *     Android:  8,5  9,8  9,9  11,3  12,3 s
 *   Cinque silenzi oltre gli 8 s in un avvio che stava andando bene. Con
 *   VM_KERNEL_SILENZIO_MS a 8000 sopravvivere era questione di DOVE cadeva il
 *   buco: chi ne prendeva uno prima della duecentesima riga veniva ucciso, chi
 *   lo prendeva dopo passava. Non e' un guardiano, e' un lancio di moneta.
 *
 * PERCHE' SOGLIE FISSE GENEROSE E NON UNA SOGLIA ADATTIVA. La prima idea era
 * ricavare il budget dal ritmo osservato -- quattro volte il buco piu' grande
 * visto finora. Le misure l'hanno bocciata: nelle prime 162 righe il buco
 * massimo e' 2,3 s su una di quelle macchine e 2,4 s sull'altra, quindi il
 * budget si sarebbe fermato a ~9 s, e piu' tardi lo stesso avvio sano ne
 * produce uno da 12,3. La coda della distribuzione non si prevede da cio' che
 * si e' visto prima, e un guardiano che la indovina a volte e' quello che
 * avevamo.
 *
 * Il costo di essere generosi e' asimmetrico, e va nella nostra direzione: un
 * guest DAVVERO morto non stampa niente, mai, quindi aspettare non cambia
 * l'esito, cambia solo quanto ci si mette a dirlo. Un guest lento invece viene
 * ucciso, e l'utente riceve una diagnosi falsa su un guasto che non ha. Un
 * minuto perso nel caso raro vale il non mentire nel caso comune. */
#define VM_ATTESA_KERNEL_MS      60000    /* minimo prima di poter giudicare:
                                             16,0 s misurati con margine 4x */
#define VM_KERNEL_SILENZIO_MS    45000    /* seriale muta per tanto = inchiodato:
                                             12,3 s misurati con margine 3,6x */
#define VM_KERNEL_TETTO_MS      300000    /* vivo ma senza arrivare: si rinuncia.
                                             Su una macchina 40 volte piu' lenta
                                             la userspace arriva dopo i due
                                             minuti di prima */
#define VM_RIGHE_SANE         200
#define VM_ATTESA_ANDROID_MS  120000
/* VENTIMILA, non trenta: winq (qemu/ui-winq/winq-window.c,
 * winq_arma_scadenza_chiusura) arma la PROPRIA scadenza di 30000 ms al suo
 * WM_CLOSE, e forza qmp_quit() se il guscio non ha finito entro quel tempo.
 * Il guscio arma la propria scadenza (vm_ora_spegni, in vm_avvia_spegnimento)
 * circa 500 ms DOPO: quello e' il periodo del timer con cui main.c nota
 * l'evento con nome che winq ha segnalato (FIN_TIMER = 500 ms in
 * finestra.c). Con lo STESSO valore nelle due scadenze, quella di winq scatta
 * SEMPRE prima, e il guscio vede QEMU uscire per lo qmp_quit forzato di winq
 * e lo scambia per un'uscita pulita: dichiara "Android si e' spento e QEMU e'
 * uscito da se'" (vedi VM_SPEGNIMENTO piu' sotto) per un guasto che invece e'
 * un'uscita FORZATA, mentre la riga "FORZO, la partizione dati potrebbe non
 * essere stata chiusa bene" resta IRRAGGIUNGIBILE su questo percorso -- due
 * messaggi opposti per lo stesso evento, secondo quale finestra si e' chiusa
 * prima. Con 20000 e' sempre il guscio a dichiarare il fallimento, che e' chi
 * lo conosce per davvero (ha visto vm_viva() restare vera). Margine 4x sui
 * 4485 ms misurati per uno spegnimento pulito completo. Se una
 * futura modifica cambia UNA delle due scadenze, va cambiata anche l'altra
 * mantenendo questo margine, o il guasto sopra si ripresenta. */
#define VM_SPEGNIMENTO_MS     20000

static DWORD vm_adesso(void)
{
    return (DWORD)GetTickCount64();
}

static void vm_vai(VmStato nuovo)
{
    vm_s = nuovo;
    vm_ora_stato = vm_adesso();
    /* L'orologio del silenzio riparte insieme allo stato. Senza, il tentativo
     * numero due erediterebbe l'istante dell'ultima riga del numero uno e
     * verrebbe dichiarato muto prima di aver avuto modo di dire qualcosa. */
    vm_ora_ultima_riga = vm_ora_stato;
}

/* Il thread che svuota lo stderr di QEMU.
 *
 * OBBLIGATORIO, non comodo: un tubo che nessuno svuota si riempie e BLOCCA chi
 * scrive, cioe' QEMU. Lo stderr va in un tubo invece che in un file -- come fa
 * lo script -- per poterlo mostrare vivo invece che dopo. */
static DWORD WINAPI vm_thread_stderr_corpo(LPVOID p)
{
    HANDLE h = (HANDLE)p;
    char pezzo[1024];
    char riga[512];
    int in_riga = 0;
    DWORD letti;

    while (ReadFile(h, pezzo, sizeof(pezzo), &letti, NULL) && letti) {
        DWORD i;

        for (i = 0; i < letti; i++) {
            if (pezzo[i] == '\n' || in_riga >= (int)sizeof(riga) - 1) {
                riga[in_riga] = '\0';
                if (in_riga) {
                    registro_riga(REG_QEMU, "%s", riga);
                }
                in_riga = 0;
            } else if (pezzo[i] != '\r') {
                riga[in_riga++] = pezzo[i];
            }
        }
    }
    if (in_riga) {
        riga[in_riga] = '\0';
        registro_riga(REG_QEMU, "%s", riga);
    }
    return 0;
}

/* Conta le righe di seriale prodotte finora, e ne riversa le nuove nel registro.
 *
 * LA SERIALE RESTA UN FILE e la si segue, perche' il backend "pipe:" di QEMU su
 * Windows usa i named pipe ed e' delicato. Tenerlo semplice qui costa nulla. */
/* La riga e' una delle ripetizioni della ricerca dell'HAL OMX?
 *
 * COSA SUCCEDE, misurato su un'installazione appena fatta:
 * mediaserver chiama getService() su android.hardware.media.omx@1.0::IOmxStore,
 * hwservicemanager chiede a init di avviare quel servizio, init non lo trova e
 * lo scrive sulla seriale, HidlServiceManagement aspetta un secondo e RIPROVA.
 * Per sempre: 431 tentativi in sette minuti, il 20% della seriale.
 *
 * PERCHE' NON SI RISOLVE ALLA RADICE. L'HAL OMX non esiste in questa vendor.img
 * -- nessun binario in /vendor/bin/hw/, nessun .rc -- perche' l'immagine di
 * Waydroid arm64_only e' solo-Codec2, che su Android 13 e' la strada giusta. Non
 * e' un guasto e non e' nostro: i codec funzionano, media.swcodec gira, e
 * mediaserver consuma 0,0% di CPU in questa attesa. L'unico modo di togliere la
 * causa sarebbe impedire a un servizio di sistema di cercare cio' che cerca,
 * per un problema che non ha conseguenze funzionali.
 *
 * PERCHE' SI FILTRA LO STESSO. Un registro in cui una riga su cinque non
 * significa niente non e' uno strumento: fra sei mesi qualcuno cerchera' li'
 * dentro un guasto vero. Ma il filtro CONTA e DICHIARA (vedi la riga di
 * riepilogo in vm_segui_seriale), cosi' non nasconde: dice quante ne ha tolte.
 *
 * Il confronto e' sulla stringa esatta del nome del servizio, non su "omx": un
 * messaggio diverso che parli di OMX deve continuare a comparire. */
static bool vm_riga_omx(const char *riga)
{
    return strstr(riga, "android.hardware.media.omx@1.0::IOmxStore") != NULL &&
           (strstr(riga, "Could not find") != NULL ||
            strstr(riga, "ctl.interface_start") != NULL);
}

static int vm_segui_seriale(void)
{
    char riga[512];
    int righe = 0;

    if (!vm_seriale) {
        vm_seriale = fopen("guest/logs/sessione-viva.log", "rb");
        if (!vm_seriale) {
            return 0;
        }
        vm_seriale_pos = 0;
    }
    fseek(vm_seriale, vm_seriale_pos, SEEK_SET);
    while (fgets(riga, sizeof(riga), vm_seriale)) {
        size_t l = strlen(riga);

        if (l == sizeof(riga) - 1 && riga[l - 1] != '\n') {
            /* Il buffer si e' riempito SENZA trovare un newline: due casi
             * possibili, e vanno distinti perche' uno blocca il conteggio
             * per sempre. Si guarda se oltre questi 511 byte il file ha
             * gia' altro (fgetc/ungetc, senza consumare se non c'e'). */
            long pos_dopo = ftell(vm_seriale);
            int prossimo = fgetc(vm_seriale);

            if (prossimo == EOF) {
                /* Non c'e' altro oltre quello che fgets ha gia' preso: QEMU
                 * sta davvero ancora scrivendo questa riga. Ci si ferma SENZA
                 * avanzare vm_seriale_pos, cosi' il prossimo giro rilegge lo
                 * stesso prefisso e vede il resto non appena arriva -- e'
                 * l'unico caso in cui fermarsi senza consumare e' corretto. */
                break;
            }
            /* C'e' dell'altro dopo: la riga e' GENUINAMENTE piu' lunga dei
             * 511 caratteri del buffer, non semplicemente "ancora in
             * scrittura". Rileggerla per sempre dalla stessa vm_seriale_pos
             * bloccherebbe il conteggio delle righe in modo permanente,
             * proprio nella finestra di VM_ATTESA_KERNEL che decide se il
             * kernel e' sano: un avvio sano verrebbe dichiarato inchiodato.
             * Sui log reali la riga piu' lunga vista finora e' circa 333
             * caratteri (sotto i 511), ma un panic o un dump SELinux verboso
             * possono superarli. Si CONSUMA il frammento: si rimette indietro
             * il byte appena sbirciato, si registra come riga TRONCATA (non
             * la si scarta, per non perdere quello che dice), e si avanza
             * vm_seriale_pos cosi' il giro successivo prosegue oltre. */
            ungetc(prossimo, vm_seriale);
            registro_riga(REG_GUEST, "%s...(line truncated, over 511 characters)",
                          riga);
            vm_seriale_pos = pos_dopo;
            righe++;
            continue;
        }
        if (l && riga[l - 1] != '\n') {
            /* riga incompleta e piu' corta del buffer: qui non c'e'
             * ambiguita', fgets puo' essersi fermata cosi' solo per fine
             * file. QEMU sta ancora scrivendo. Si aspetta il resto. */
            break;
        }
        riga[l - 1] = '\0';
        if (vm_riga_omx(riga)) {
            /* Si sopprime, ma si CONTA come riga vista: il conteggio serve al
             * guardiano dell'avvio (VM_RIGHE_SANE), e togliere righe dal conto
             * gli farebbe credere inchiodato un guest che sta parlando. */
            vm_omx_soppresse++;
            /* La prima si mostra sempre, e le successive si riassumono ogni
             * cento: chi legge deve sapere che il fenomeno esiste, non vederlo
             * quattrocento volte. */
            if (vm_omx_soppresse == 1) {
                registro_riga(REG_GUSCIO, "the guest keeps looking for the OMX "
                              "media HAL, which this vendor image does not "
                              "ship (it is Codec2-only). Harmless: codecs work "
                              "through Codec2. These lines are suppressed from "
                              "here on and counted.");
                vm_omx_dette = 1;
            } else if (vm_omx_soppresse - vm_omx_dette >= 100) {
                registro_riga(REG_GUSCIO, "OMX HAL lookups suppressed so far: "
                              "%lu", vm_omx_soppresse);
                vm_omx_dette = vm_omx_soppresse;
            }
            vm_seriale_pos = ftell(vm_seriale);
            righe++;
            continue;
        }
        registro_riga(REG_GUEST, "%s", riga);
        vm_seriale_pos = ftell(vm_seriale);
        righe++;
    }
    return righe;
}

static int vm_righe_totali;
/* Il conteggio di righe a cui si e' bloccato il tentativo PRECEDENTE: serve
 * al secondo degrado (il muro), vedi il commento in VM_AVVIA. -1 = nessun
 * blocco ancora. */
static int vm_righe_blocco_prec = -1;

static bool vm_lancia(void)
{
    char argomenti[4096];
    char riga[4600];
    SECURITY_ATTRIBUTES sa = {0};
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    HANDLE scrivi = NULL;

    if (!vm_argomenti(vm_c, argomenti, sizeof(argomenti))) {
        registro_riga(REG_GUSCIO, "the arguments do not fit the buffer: this is a "
                                  "defect of the shell, not of the configuration");
        return false;
    }
    snprintf(riga, sizeof(riga), "runtime\\bin\\qemu-nostro.exe %s", argomenti);

    /* La seriale ATTIVA deve nascere pulita a ogni tentativo: un log vecchio
     * farebbe contare righe di un avvio precedente e dichiarare sano un kernel
     * inchiodato. Il difetto e' gia' capitato leggendo un boot_completed di due
     * ore prima come se fosse quello appena fatto.
     *
     * MA CANCELLARLA DISTRUGGEVA LE PROVE. system_server e' morto
     * durante un install, e la riga del kernel che nominava il pid giusto e'
     * andata perduta al riavvio successivo: di quel crash restano solo le righe
     * copiate a mano prima di riavviare. Con un difetto che si presenta una volta
     * ogni qualche ora, ogni occorrenza persa costa una riproduzione intera.
     * Quindi si SPOSTA invece di cancellare, e la proprieta' di sopra resta
     * intatta perche' il file attivo non c'e' piu' comunque. */
    {
        SYSTEMTIME ora;

        GetLocalTime(&ora);
        if (archivio_ruota("guest/logs/sessione-viva.log", ARCH_CARTELLA,
                           "sessione-viva", ARCH_QUANTI, &ora)) {
            registro_riga(REG_GUSCIO, "the previous run's serial log is in "
                          "%s (%d are kept)", ARCH_CARTELLA, ARCH_QUANTI);
        }
    }
    if (vm_seriale) {
        fclose(vm_seriale);
        vm_seriale = NULL;
    }
    vm_righe_totali = 0;
    /* Si azzera anche qui, non solo dentro VM_ATTESA_ANDROID: cosi' il primo
     * controllo di adb_pronto in quello stato scatta subito, invece di
     * aspettare 2 s residui di un tentativo precedente che non c'entra piu'. */
    vm_ora_ultima_adb = 0;

    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&vm_stderr_lettura, &scrivi, &sa, 0)) {
        registro_riga(REG_GUSCIO, "CreatePipe for stderr failed (%lu)",
                      GetLastError());
        return false;
    }
    SetHandleInformation(vm_stderr_lettura, HANDLE_FLAG_INHERIT, 0);

    /* WINQ_GL per il figlio. Si imposta nell'ambiente NOSTRO perche' il figlio
     * lo eredita: CreateProcess con lpEnvironment a NULL passa l'ambiente del
     * chiamante. Costruire un blocco di ambiente a mano per una variabile
     * sarebbe piu' codice e nessun vantaggio, dato che il guscio non ne usa
     * altre. */
    SetEnvironmentVariableA("WINQ_GL", vm_c->gl);

    /* MESA_WGL_NO_FLUSH_WAIT per il figlio, stessa via di WINQ_GL qui sopra.
     * La legge la NOSTRA opengl32.dll (Mesa 26.2.0 + i patch in
     * research/mesa-patches/), quindi vale solo sul percorso wgl: su ANGLE
     * nessuno la guarda, e impostarla non fa nulla.
     *
     * CHI DECIDE, e perche' non e' il config a vincere sempre. Se la variabile
     * e' GIA' nell'ambiente non la si tocca: e' la leva con cui si fanno gli
     * A/B (guest/scripts/ab-no-flush-wait.sh mette i due bracci uno contro
     * l'altro proprio cosi'), e sovrascriverla renderebbe l'esperimento un
     * confronto fra due corse identiche -- il modo peggiore di sbagliare, dato
     * che produce numeri invece di un errore. Stessa disciplina di WINQ_HZ
     * poco sotto, che il guscio imposta solo quando il config dice qualcosa di
     * esplicito. Chi vuole l'attesa in modo permanente scrive
     * gl_flush_wait=yes in config.txt; chi la vuole per una corsa sola esporta
     * la variabile e il guscio si fa da parte, dicendolo nel registro. */
    SetEnvironmentVariableA("WINQ_GL_FLUSH_WAIT",
                            vm_c->gl_attesa_flush ? "yes" : "no");

    /* MESA_WGL_NO_FLUSH_WAIT non la impostiamo PIU'.
     *
     * COSA E' CAMBIATO IL. Quella variabile cambiava la semantica di
     * GL dentro Mesa per chiunque girasse nel processo, ed era il modo
     * sbagliato di chiedere una cosa che la specifica prevede: adesso winq
     * chiede WGL_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB a wglCreateContextAttribsARB
     * (WGL_ARB_context_flush_control, implementata nella nostra opengl32.dll) e
     * l'effetto vale SOLO per i contesti che lo chiedono. Il guscio quindi non
     * tocca piu' Mesa: dice a winq cosa vuole il prodotto, e winq lo chiede.
     *
     * La variabile di Mesa resta come scavalco globale per l'A/B, e proprio
     * perche' e' uno scavalco NON va impostata da qui: se il guscio la mettesse,
     * guest/scripts/ab-no-flush-wait.sh non avrebbe piu' un braccio di
     * controllo -- entrambi i bracci sarebbero senza attesa. */
    if (GetEnvironmentVariableA("MESA_WGL_NO_FLUSH_WAIT", NULL, 0) != 0) {
        registro_riga(REG_GUSCIO, "MESA_WGL_NO_FLUSH_WAIT is in the environment: "
                      "it is a GLOBAL override inside Mesa and wins over what winq "
                      "asks per context (config gl_flush_wait=%s)",
                      vm_c->gl_attesa_flush ? "yes" : "no");
    }

    /* WINQ_HZ per il figlio, stessa via di WINQ_GL qui sopra e per la stessa
     * ragione (l'ambiente nostro, non un blocco costruito a mano). Solo
     * quando hz != 0: lo 0 e' "automatico" (winq legge il pannello da se',
     * GetDeviceCaps(VREFRESH)), e non impostare la variabile e' come non
     * averla mai messa -- il comportamento di oggi, invariato. Il valore
     * arriva a QEMU per ambiente e non per riga di comando perche' winq lo
     * rilegge ad ogni cambio di scanout e ad ogni rotazione (vedi
     * winq_hz_da_annunciare in winq-window.c): una riga di comando fissa
     * varrebbe solo all'avvio. */
    if (vm_c->hz != 0) {
        char hz_buf[16];

        snprintf(hz_buf, sizeof(hz_buf), "%d", vm_c->hz);
        SetEnvironmentVariableA("WINQ_HZ", hz_buf);
    }

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError = scrivi;
    si.hStdOutput = scrivi;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    if (!CreateProcessA(NULL, riga, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        registro_riga(REG_GUSCIO, "cannot start QEMU (%lu)",
                      GetLastError());
        CloseHandle(vm_stderr_lettura);
        CloseHandle(scrivi);
        vm_stderr_lettura = NULL;
        return false;
    }
    CloseHandle(scrivi);
    CloseHandle(pi.hThread);
    vm_processo = pi.hProcess;
    vm_thread_stderr = CreateThread(NULL, 0, vm_thread_stderr_corpo,
                                    vm_stderr_lettura, 0, NULL);
    registro_riga(REG_GUSCIO, "QEMU started, pid %lu, attempt %d of %d",
                  pi.dwProcessId, vm_tentativo, vm_c->riprove);
    return true;
}

static void vm_uccidi(void)
{
    if (vm_processo) {
        DWORD esito;

        TerminateProcess(vm_processo, 1);
        esito = WaitForSingleObject(vm_processo, 5000);
        if (esito == WAIT_TIMEOUT) {
            /* Un guasto raro e silenzioso e' peggio di un guasto raro
             * annunciato: se il processo non muore in 5 s si chiude comunque
             * l'handle e si azzera il puntatore qui sotto, e il percorso di
             * riprova cancella il log della seriale e lancia una nuova
             * istanza mentre la vecchia potrebbe essere ancora viva. Chi
             * legge il registro deve poterlo sapere. */
            registro_riga(REG_GUSCIO, "QEMU did not end within 5 s of "
                          "TerminateProcess: the process may still "
                          "be alive");
        }
        CloseHandle(vm_processo);
        vm_processo = NULL;
    }
    if (vm_thread_stderr) {
        WaitForSingleObject(vm_thread_stderr, 2000);
        CloseHandle(vm_thread_stderr);
        vm_thread_stderr = NULL;
    }
    if (vm_stderr_lettura) {
        CloseHandle(vm_stderr_lettura);
        vm_stderr_lettura = NULL;
    }
}

bool vm_viva(void)
{
    return vm_processo &&
           WaitForSingleObject(vm_processo, 0) == WAIT_TIMEOUT;
}

void vm_apri(const Config *c)
{
    vm_c = c;
    vm_tentativo = 0;
    vm_usciti_subito = 0;
    vm_vai(VM_PREPARA);
}

VmStato vm_stato(void)
{
    return vm_s;
}

void vm_avvia_spegnimento(void)
{
    /* La guardia copre anche VM_MORTA e VM_FALLITA, non solo SPEGNIMENTO e
     * USCITO: se un pulsante di chiusura richiamasse questa funzione da
     * VM_MORTA (QEMU e' gia' uscito da se'), senza questo controllo si
     * entrerebbe comunque in VM_SPEGNIMENTO e al tick successivo -- col
     * processo gia' NULL -- vm_passo dichiarerebbe falsamente "Android si e'
     * spento e QEMU e' uscito da se'", cancellando la distinzione fra MORTA e
     * USCITO che il disegno protegge apposta: e' quella distinzione a decidere
     * se la finestra resta aperta (MORTA, con la ragione nel registro) o si
     * chiude (USCITO, perche' l'abbiamo chiesto noi). */
    if (vm_s == VM_SPEGNIMENTO || vm_s == VM_USCITO
        || vm_s == VM_MORTA || vm_s == VM_FALLITA) {
        return;
    }
    vm_ora_spegni = vm_adesso();
    vm_vai(VM_SPEGNIMENTO);
    /* LIMITE NOTO: adb_spegni chiama esegui_catturando in modo SINCRONO, sullo
     * stesso thread della finestra, con la stessa scadenza di 20 s di ogni
     * comando adb (vedi adb.c). Al clic di chiusura il guscio puo' quindi
     * restare fermo fino a 20 s se adb si e' impiantato. E' un colpo solo,
     * chiamato una volta sola qui e non ad ogni tick come adb_pronto in
     * VM_ATTESA_ANDROID: per questo non serve renderlo asincrono in questo
     * task. Ma il blocco va dichiarato, perche' altrimenti una finestra ferma
     * al momento della chiusura, senza spiegazione, sembra un guasto. */
    if (!adb_spegni(vm_c)) {
        registro_riga(REG_GUSCIO, "adb did not accept reboot -p: waiting "
                                  "anyway, then forcing");
    }
}

/* Decide COSA fare quando l'utente chiude la finestra di Android, in funzione
 * dello stato in cui si trova la VM in quel momento.
 *
 * ESTRATTA APPOSTA in una funzione pura: prima questa decisione era un "if" a
 * DUE rami inline nel gestore di WM_APP+1 in main.c (vm_stato() == VM_PRONTO
 * || vm_stato() == VM_ATTESA_ANDROID -> spegni, altrimenti -> uccidi e
 * distruggi subito), su NOVE stati possibili. Nessuna prova poteva
 * raggiungerla li' dentro. E' la decisione dietro il BLOCCANTE CRITICO
 * "chiudere due volte stacca la corrente al guest": VM_SPEGNIMENTO cadeva nel
 * ramo "altrimenti", cioe' un secondo clic (o un doppio Alt+F4, o lo
 * spegnimento di Windows) durante i secondi in cui adb_spegni() e' gia' in
 * corso uccideva QEMU SUBITO, a meta' di un reboot -p, su data.img montato
 * cache=writeback: la partizione dati puo' corrompersi, e nel registro non
 * compare nulla perche' main.c usciva prima di scrivere qualunque riga. La
 * guardia di rientro in vm_avvia_spegnimento (sopra) esiste ed e' corretta,
 * ma non veniva MAI raggiunta: il chiamante decideva prima di lei. */
AzioneChiusura vm_azione_chiusura(VmStato s)
{
    switch (s) {
    case VM_ATTESA_ANDROID:
        /* Il kernel e' partito ma adb potrebbe gia' rispondere anche prima
         * che sys.boot_completed sia dichiarato: si tenta comunque lo
         * spegnimento pulito, come si faceva gia' oggi per questo stato
         * (vedi il confronto in main.c prima di questa estrazione). Se adb
         * non risponde, adb_spegni fallisce e lo registra lui; si resta in
         * VM_SPEGNIMENTO comunque, e la sua scadenza forza. */
    case VM_PRONTO:
        return CHIUSURA_SPEGNI;

    case VM_SPEGNIMENTO:
        /* Uno spegnimento e' GIA' in corso: e' esattamente il caso del
         * Bloccante Critico. Richiamare vm_avvia_spegnimento non farebbe
         * danno da solo (la sua guardia di rientro lo blocca), ma il vecchio
         * ramo "altrimenti" di main.c non chiamava vm_avvia_spegnimento:
         * chiamava vm_chiudi() + DestroyWindow(), cioe' uccideva SUBITO.
         * Qui si IGNORA di proposito: la scadenza di VM_SPEGNIMENTO_MS in
         * vm_passo forza comunque l'uscita, quindi ignorare e' sicuro e non
         * lascia il guscio bloccato per sempre. */
        return CHIUSURA_ATTENDI;

    case VM_AVVIA:
    case VM_ATTESA_KERNEL:
        /* Uscita dura, ma INEVITABILE, non solo la piu' comoda: in questi due
         * stati adbd non esiste ancora nel guest (VM_ATTESA_KERNEL dura
         * VM_ATTESA_KERNEL_MS = 16000 ms dall'avvio, e adbd nel guest parte
         * molto dopo), quindi non c'e' nessuno a cui chiedere "reboot -p".
         * Il chiamante DEVE dichiararlo (vedi main.c), non tacerlo: altrimenti
         * l'utente crede a un'uscita pulita che non e' mai potuta avvenire. */
        return CHIUSURA_DURA;

    case VM_PREPARA:
    case VM_USCITO:
    case VM_MORTA:
    case VM_FALLITA:
    default:
        /* Nessuno di questi quattro stati ha un processo QEMU da spegnere
         * pulito: VM_PREPARA non ne ha ancora creato uno (sta solo
         * verificando i file), VM_USCITO e' gia' morto perche' l'abbiamo
         * chiesto noi, VM_MORTA e' uscito da se' e VM_FALLITA non e' mai
         * partito con successo (o si e' arreso dopo vm_c->riprove tentativi).
         * "Uccidere" qui e' un no-op sicuro (vm_uccidi controlla vm_processo
         * prima di toccare nulla) e chiudere e' gia' l'azione giusta: non
         * c'e' nulla da attendere ne' da spegnere per davvero. */
        return CHIUSURA_DURA;
    }
}

VmStato vm_passo(void)
{
    switch (vm_s) {
    case VM_PREPARA: {
        char mancante[MAX_PATH];

        if (!vm_verifica_file(vm_c, mancante, sizeof(mancante))) {
            /* DUE FAMIGLIE DI FILE MANCANTI, DUE MESSAGGI DIVERSI, perche' un
             * unico messaggio ("ricostruirli con i copioni di sviluppo") era
             * VERO solo per meta' dei casi e FALSO -- un vicolo cieco -- per
             * l'altra meta'.
             *
             * vendor.img (vm_necessari_fissi[2]) e la system.img della
             * variante attiva NON viaggiano nel pacchetto di rilascio: si
             * scaricano da sole al primo avvio (vendor_prepara e
             * variante_prepara_attiva, in main.c, chiamate PRIMA di vm_apri,
             * cioe' prima che si possa mai arrivare qui con VM_PREPARA). Se
             * una delle due manca comunque -- quel tentativo non e' riuscito,
             * o il file e' stato tolto a mano dopo un avvio riuscito -- la
             * causa vera e' quasi sempre la rete, MAI un ambiente di sviluppo
             * assente: dire "ricostruirlo con build-guest-kernel.sh" a chi ha
             * solo scompattato uno zip sarebbe una diagnosi falsa quanto il
             * kernel "inchiodato" che questo intero rimedio esiste per non
             * mostrare.
             *
             * Il kernel, initramfs.img e qemu-nostro.exe INVECE viaggiano
             * davvero nel pacchetto (o sono l'eseguibile stesso): se mancano
             * e' il pacchetto a essere incompleto o l'ambiente a essere quello
             * di uno sviluppatore, e il messaggio che rimanda ai copioni di
             * build resta quello giusto -- sensato pero' SOLO per chi
             * sviluppa, e resta un limite dichiarato, non una correzione di
             * questo blocco. */
            const char *immagine_attiva;

            var_percorsi(vm_c->variante, &immagine_attiva, NULL);
            if (!strcmp(mancante, vm_necessari_fissi[2]) ||
                !strcmp(mancante, immagine_attiva)) {
                registro_riga(REG_GUSCIO, "%s is missing: it is an image that "
                              "downloads itself on first run, so an "
                              "Internet connection is needed. Check the "
                              "connection and start Habumi again.",
                              mancante);
            } else {
                registro_riga(REG_GUSCIO, "%s is missing. If it is the kernel or "
                              "the initramfs, rebuild them with "
                              "guest/scripts/build-guest-kernel.sh e "
                              "guest/scripts/build-all.ps1; if it is adb.exe, "
                              "copy it next to this executable.",
                              mancante);
            }
            vm_vai(VM_FALLITA);
            break;
        }
        vm_vai(VM_AVVIA);
        break;
    }

    case VM_AVVIA:
        vm_tentativo++;
        if (vm_tentativo > vm_c->riprove) {
            /* DUE GUASTI DIVERSI, e prima si diceva sempre il secondo.
             *
             * Se QEMU e' uscito subito ogni volta, il kernel non e' MAI
             * partito: la ragione sta nelle righe [qemu] e riprovare non serve
             * a niente, perche' non c'e' nessuna corsa da rigiocare. Dirgli
             * "il kernel si e' inchiodato" manda a cercare un guasto non
             * deterministico dove c'e' invece qualcosa di rotto e stabile --
             * e' successo davvero: una cartella guest/logs mancante nel
             * pacchetto di rilascio faceva fallire il -serial di QEMU, e il
             * verdetto incolpava il kernel.
             *
             * La corsa sotto WHPX e' reale, ma e' l'ALTRO caso: QEMU resta
             * vivo e la seriale non arriva. */
            if (vm_usciti_subito >= vm_tentativo - 1) {
                registro_riga(REG_GUSCIO, "%d attempts failed: QEMU exited "
                              "immediately EVERY time, so the kernel never "
                              "started. The reason is in the [qemu] lines "
                              "above -- this is not the non-deterministic "
                              "start-up fault, and retrying does not help.",
                              vm_c->riprove);
                vm_vai(VM_FALLITA);
                break;
            }
            registro_riga(REG_GUSCIO, "%d attempts failed, down to %d vCPU. "
                          "If every attempt produced ZERO serial lines the "
                          "guest never ran at all, which on some machines is "
                          "WHPX failing to create the vCPUs; if it produced "
                          "some and then stopped, look at the [guest] lines "
                          "for where it stopped.", vm_c->riprove, vm_vcpu_ora);
            vm_vai(VM_FALLITA);
            break;
        }
        /* Quanti vCPU per QUESTO tentativo. vm_righe_totali qui contiene
         * ancora il conto del tentativo precedente: vm_lancia lo azzera dopo.
         *
         * IL SECONDO DEGRADO, quello del muro. Il primo (zero righe) copre il
         * guest che non esegue nemmeno un'istruzione. Ma sull'issue #1 e'
         * arrivato l'altro caso: tre tentativi a 6 vCPU morti TUTTI a 164
         * righe, sulla stessa identica riga -- il primo initcall che aspetta
         * una risposta da un'altra CPU, con gli interrupt fra processori che
         * su quella macchina a volte si perdono anche con le feature
         * sintetiche concesse. Non e' una corsa da rigiocare: e' un muro, e
         * si sposta solo col numero di vCPU. Due blocchi consecutivi sullo
         * STESSO conteggio di righe non capitano per varianza (gli avvii veri
         * oscillano di decine di righe): da li' in poi si dimezza, come per
         * lo zero. */
        if (vm_tentativo == 1) {
            vm_vcpu_ora = vm_c->vcpu;
            vm_righe_blocco_prec = -1;
        } else if (vm_righe_totali == 0 && vm_vcpu_ora > 1) {
            int prima = vm_vcpu_ora;

            vm_vcpu_ora /= 2;
            if (vm_vcpu_ora < 1) {
                vm_vcpu_ora = 1;
            }
            registro_riga(REG_GUSCIO, "the last attempt produced no serial "
                          "output at all, so the guest never ran a single "
                          "instruction: retrying with %d vCPU instead of %d",
                          vm_vcpu_ora, prima);
        } else if (vm_righe_totali > 0 &&
                   vm_righe_totali == vm_righe_blocco_prec &&
                   vm_vcpu_ora > 1) {
            int prima = vm_vcpu_ora;

            vm_vcpu_ora /= 2;
            if (vm_vcpu_ora < 1) {
                vm_vcpu_ora = 1;
            }
            registro_riga(REG_GUSCIO, "two attempts in a row hung at the "
                          "same line (%d): that is a wall, not a race, and "
                          "on some machines it moves with the vCPU count. "
                          "Retrying with %d vCPU instead of %d",
                          vm_righe_totali, vm_vcpu_ora, prima);
        }
        vm_righe_blocco_prec = vm_righe_totali;

        if (!vm_lancia()) {
            vm_vai(VM_FALLITA);
            break;
        }
        vm_vai(VM_ATTESA_KERNEL);
        break;

    case VM_ATTESA_KERNEL: {
        int nuove = vm_segui_seriale();
        DWORD da_avvio, da_riga;

        vm_righe_totali += nuove;
        if (nuove > 0) {
            vm_ora_ultima_riga = vm_adesso();
        }
        if (!vm_viva()) {
            registro_riga(REG_GUSCIO, "QEMU exited immediately: the reason is "
                                      "in the [qemu] lines above");
            vm_usciti_subito++;
            vm_uccidi();
            vm_vai(VM_AVVIA);
            break;
        }

        /* Duecento righe bastano APPENA ARRIVANO, non a una scadenza: su una
         * macchina veloce succede prima dei sedici secondi, e non c'e' ragione
         * di farla aspettare. */
        if (vm_righe_totali < VM_RIGHE_SANE) {
            da_avvio = vm_adesso() - vm_ora_stato;
            da_riga  = vm_adesso() - vm_ora_ultima_riga;

            /* Prima del minimo di grazia non si giudica: le prime righe di un
             * kernel possono tardare qualche secondo anche dove tutto va bene. */
            if (da_avvio < VM_ATTESA_KERNEL_MS) {
                break;
            }
            /* Le righe arrivano ancora: e' lento, non inchiodato. Si aspetta,
             * fino al tetto. */
            if (da_riga < VM_KERNEL_SILENZIO_MS && da_avvio < VM_KERNEL_TETTO_MS) {
                break;
            }
            /* Il messaggio dice QUALE dei due e' successo, perche' mandano a
             * cercare in posti diversi: una seriale muta e' un guest fermo, un
             * tetto raggiunto e' un guest vivo e troppo lento per noi. */
            if (da_riga >= VM_KERNEL_SILENZIO_MS) {
                registro_riga(REG_GUSCIO, "boot hung: %d serial lines, then "
                              "nothing for %lu s -- more than the %lu s a live "
                              "boot is allowed to go quiet (needs %d lines): "
                              "closing and retrying",
                              vm_righe_totali, (unsigned long)(da_riga / 1000),
                              (unsigned long)(VM_KERNEL_SILENZIO_MS / 1000),
                              VM_RIGHE_SANE);
            } else {
                registro_riga(REG_GUSCIO, "boot still going after %lu s but "
                              "only %d of %d lines -- too slow to wait for: "
                              "closing and retrying",
                              (unsigned long)(da_avvio / 1000),
                              vm_righe_totali, VM_RIGHE_SANE);
            }
            vm_uccidi();
            vm_vai(VM_AVVIA);
            break;
        }
        registro_riga(REG_GUSCIO, "the kernel started (%d serial lines)",
                      vm_righe_totali);
        /* Se e stato il degrado a farlo partire l utente deve saperlo, o alla
         * prossima esecuzione ripaghera i tentativi falliti daccapo. */
        if (vm_vcpu_ora != vm_c->vcpu) {
            registro_riga(REG_GUSCIO, "it started with %d vCPU, not the %d in "
                          "config.txt. Put vcpu=%d in runtime/bin/config.txt to "
                          "skip the failed attempts next time.",
                          vm_vcpu_ora, vm_c->vcpu, vm_vcpu_ora);
        }
        vm_vai(VM_ATTESA_ANDROID);
        break;
    }

    case VM_ATTESA_ANDROID:
        vm_righe_totali += vm_segui_seriale();
        if (!vm_viva()) {
            registro_riga(REG_GUSCIO, "QEMU exited while Android was booting");
            vm_uccidi();
            vm_vai(VM_FALLITA);
            break;
        }
        /* IL FRENO A 2 SECONDI, misurato: adb_pronto lancia 1 o 2 processi
         * SINCRONI tramite esegui_catturando, che costano 100-300 ms l'uno
         * quando adb risponde e FINO A 20 S INTERI se adb si e' impiantato
         * (vedi la scadenza in adb.c). Il timer di vm_passo batte ogni 500
         * ms: senza freno, nei due minuti di questo stato adb_pronto verrebbe
         * chiamata fino a 240 volte, e un solo adb impiantato bloccherebbe
         * proprio il thread che deve restare reattivo -- quello della
         * finestra -- per 20 s, cioe' esattamente il difetto che questa
         * macchina a stati esiste per eliminare. Si interroga adb al massimo
         * ogni 2 s, come dice la tabella degli stati del piano: negli altri
         * tick si insegue solo la seriale e si controlla che QEMU sia vivo,
         * che non costano nulla. */
        if (vm_adesso() - vm_ora_ultima_adb >= 2000) {
            vm_ora_ultima_adb = vm_adesso();
            if (adb_pronto(vm_c)) {
                registro_riga(REG_GUSCIO, "Android is ready");
                vm_vai(VM_PRONTO);
                break;
            }
        }
        if (vm_adesso() - vm_ora_stato > VM_ATTESA_ANDROID_MS) {
            /* NON si va in VM_PRONTO: quello stato accende i bottoni, e i
             * bottoni passano da adb. Se adb non risponde, bottoni attivi che
             * non fanno nulla sarebbero peggio di bottoni grigi -- l'utente
             * crederebbe rotto Android invece del canale. Si resta in attesa e
             * si dichiara, cosi' la VM e' usabile col mouse e col dito mentre il
             * registro dice cosa manca. */
            registro_riga(REG_GUSCIO, "no sys.boot_completed within two "
                          "minutes. The Android window stays usable with "
                          "touch and keyboard, but the buttons stay grey and "
                          "closing will be HARD: without adb there is no clean "
                          "shutdown. Look at the [guest] lines to see where "
                          "the boot stopped.");
            vm_ora_stato = vm_adesso();   /* si ridichiara ogni due minuti */
        }
        break;

    case VM_PRONTO:
        vm_righe_totali += vm_segui_seriale();
        if (!vm_viva()) {
            /* VM_MORTA e non VM_USCITO, e la differenza conta: qui QEMU e'
             * uscito senza che nessuno gliel'abbia chiesto. Chiudere il guscio
             * porterebbe via con se' il registro, cioe' l'unico posto dove sta
             * la ragione. Si resta aperti finche' l'utente non chiude. */
            registro_riga(REG_GUSCIO, "QEMU exited on its own. The window stays "
                          "open: the reason is in the lines above.");
            vm_uccidi();
            vm_vai(VM_MORTA);
        }
        break;

    case VM_SPEGNIMENTO:
        vm_righe_totali += vm_segui_seriale();
        if (!vm_viva()) {
            registro_riga(REG_GUSCIO, "Android shut down and QEMU exited on "
                          "its own in %lu ms", vm_adesso() - vm_ora_spegni);
            vm_uccidi();
            vm_vai(VM_USCITO);
            break;
        }
        if (vm_adesso() - vm_ora_spegni > VM_SPEGNIMENTO_MS) {
            registro_riga(REG_GUSCIO, "shutdown went past %d s: FORCING. "
                          "The data partition may not have been closed "
                          "cleanly.", VM_SPEGNIMENTO_MS / 1000);
            vm_uccidi();
            vm_vai(VM_USCITO);
        }
        break;

    case VM_USCITO:
    case VM_MORTA:
    case VM_FALLITA:
        break;
    }
    return vm_s;
}

void vm_chiudi(void)
{
    vm_uccidi();
    if (vm_seriale) {
        fclose(vm_seriale);
        vm_seriale = NULL;
    }
}
