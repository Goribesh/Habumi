#!/bin/busybox sh
# PID 1 dell'initramfs: prepara le partizioni e passa il controllo a init di
# Android in **seconda fase**.
#
# Perche' esiste. Le immagini di Waydroid non hanno un percorso di avvio
# proprio: il vendor non contiene alcun etc/fstab*, perche' si aspettano che
# system e vendor vengano montate da fuori, come fa il container LXC. La prima
# fase di init di Android e' esattamente il codice che pretende quel fstab,
# quindi non la si puo' usare. Ma init sceglie la fase dall'argomento:
#
#     init                 -> FirstStageMain   (vuole il fstab, non ce l'abbiamo)
#     init selinux_setup   -> carica la policy
#     init second_stage    -> SecondStageMain  (si aspetta tutto gia' montato)
#
# Verificato con `strings` sul binario dell'immagine: conosce tutte e tre.
# Quindi montiamo noi, e chiamiamo la terza.
#
# Il vendor viene esposto come overlay invece che modificato: l'immagine e'
# piena al 100%, zero byte liberi, e aggiungerci hwcomposer.drm.so vorrebbe dire
# ingrandirla e riscriverla ogni volta che cambia una proprieta'. Con overlayfs
# l'immagine resta intatta e le modifiche vivono in un tmpfs.

set -x
export PATH=/bin:/sbin

/bin/busybox --install -s /bin

mkdir -p /proc /sys /dev /tmp /android
mount -t proc     proc     /proc
mount -t sysfs    sysfs    /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts

# Quello che FirstStageMain prepara e SecondStageMain da' per scontato. Saltando
# la prima fase salta anche questo, e l'effetto e' un abort che nomina solo il
# sintomo: senza /dev/socket, StartPropertyService non riesce a mettersi in
# ascolto e init si uccide con signal 6.
mkdir -p /dev/socket /dev/dm-user
chmod 0755 /dev/socket /dev/dm-user

# I device binder. Il kernel ha CONFIG_ANDROID_BINDERFS=y, e con binderfs il
# driver NON crea piu' i misc device statici: i tre nomi elencati in
# CONFIG_ANDROID_BINDER_DEVICES ("binder,hwbinder,vndbinder") compaiono soltanto
# dentro un binderfs montato. Nessuno lo monta: l'init.rc di queste immagini non
# lo fa perche' Waydroid creava i binder sull'host e li innestava nel container.
#
# Senza di questi, servicemanager, hwservicemanager e vold abortiscono tutti con
# "Binder driver could not be opened", e il sintomo che si vede in cima e' solo
# il riavvio con vold-failed.
mkdir -p /dev/binderfs
mount -t binder binder /dev/binderfs || { echo "WINQ-FERMO: binderfs non montabile" > /dev/kmsg; exec sh; }
# binderfs crea i device 0600 root:root. servicemanager gira come utente system e
# non riesce ad aprirli: aborta, nessuno diventa context manager, e ogni processo
# resta a girare su "Not able to get context object on /dev/binder". Nel percorso
# normale li porta a 0666 l'init.rc subito dopo aver montato binderfs; qui
# binderfs non lo monta nessuno, quindi nemmeno il chmod avviene.
chmod 0666 /dev/binderfs/binder /dev/binderfs/hwbinder /dev/binderfs/vndbinder
ln -sf /dev/binderfs/binder     /dev/binder
ln -sf /dev/binderfs/hwbinder   /dev/hwbinder
ln -sf /dev/binderfs/vndbinder  /dev/vndbinder
# Una scrittura per riga: ogni write() su /dev/kmsg diventa un record di printk a
# se', quindi redirigere l'output di ls produce un carattere per riga.
ls -l /dev/binderfs | while read -r l; do echo "=== winq: binderfs $l" > /dev/kmsg; done

# --- nodi DRM ---------------------------------------------------------------
# devtmpfs li crea 0600, e SurfaceFlinger gira come system: non riesce ad aprirli,
# Mesa non trova il render node e ripiega su llvmpipe. Il sintomo sono i thread
# llvmpipe-0..5 dentro surfaceflinger, cioe' rasterizzazione software mentre la
# GPU sta a guardare.
#
# /system/etc/ueventd.rc contiene gia' la regola giusta
#     /dev/dri/*   0666   root   graphics
# e il gruppo infatti risulta graphics, ma il modo resta 0600: ueventd trova i
# nodi gia' esistenti, creati qui da devtmpfs, e non ne corregge i permessi.
# Quindi li correggiamo dove nascono.
if [ -d /dev/dri ]; then
    chmod 0666 /dev/dri/* 2>/dev/null
    ls -l /dev/dri | while read -r l; do echo "=== winq: dri $l" > /dev/kmsg; done
fi

# --- nodi di input ----------------------------------------------------------
# Terza volta che incontriamo lo stesso schema, dopo binderfs e /dev/dri: devtmpfs
# crea i nodi 0600, ueventd li trova esistenti e ne corregge solo il gruppo. La
# regola giusta e' gia' in /system/etc/ueventd.rc
#     /dev/input/*   0660   root   input
# ma il modo resta 0600, quindi EventHub non apre nulla e InputReader riporta
#     Input Reader State (Nums of device: 1)
#       Device -1: Virtual
# cioe' solo la tastiera virtuale interna: la finestra non reagisce a mouse ne'
# tastiera, e sembra un problema di QEMU quando invece sono i permessi.
if [ -d /dev/input ]; then
    chmod 0660 /dev/input/* 2>/dev/null
    ls -l /dev/input | while read -r l; do echo "=== winq: input $l" > /dev/kmsg; done
fi

# --- heap dmabuf --------------------------------------------------------------
# Quarta volta lo stesso schema, ed e' quello che rende MUTO il guest. devtmpfs
# crea /dev/dma_heap/system a 0600 system:system; ueventd trova il nodo gia'
# esistente e ne sistema solo il gruppo. /system/etc/ueventd.rc lo vorrebbe cosi'
#     /dev/dma_heap/system   0444   system   system
# ma il modo resta 0600, e media.swcodec gira come utente "mediacodec", non
# "system": non riesce ad aprire l'heap, quindi Codec2 non alloca i buffer.
#
# MISURATO, e il sintomo e' lontanissimo dalla causa:
#     E MediaCodec: Codec reported err 0xfffffff4/NO_MEMORY ... state 5/STARTING
#     E NuPlayerDecoder: Failed to start [c2.android.vorbis.decoder] decoder (err=-12)
# cioe' "manca memoria" con 6 GB liberi. Il decodificatore si CREA e non PARTE, e
# quello che l'utente sente e' silenzio: nessuna suoneria, nessuna notifica,
# nessun file audio. Chi parte dall'audio guarda l'HAL, asound.conf e il PCM ALSA
# -- che infatti resta "closed" -- e non trova niente, perche' il guasto sta nel
# permesso di un nodo che con l'audio non c'entra.
#
# Si applica 0444, che e' il modo scritto nell'immagine: non un numero scelto da
# noi. Gli altri heap (default_cma_region, reserved) ueventd non li dichiara,
# quindi restano come sono.
if [ -d /dev/dma_heap ]; then
    chmod 0444 /dev/dma_heap/system* 2>/dev/null
    ls -l /dev/dma_heap | while read -r l; do echo "=== winq: dma_heap $l" > /dev/kmsg; done
fi

# --- SELinux ----------------------------------------------------------------
# Va montato qui, mentre /sys e' ancora al suo posto: piu' sotto lo spostiamo
# dentro la radice di Android, e da quel momento /sys/fs/selinux non esiste piu'
# come punto di montaggio valido.
#
# Con SELinux nel kernel la catena corretta di Android e':
#     init selinux_setup   carica la policy da /system/etc/selinux, imposta
#                          permissive o enforcing secondo androidboot.selinux,
#                          e poi fa exec di se stesso con "second_stage"
# Andare diretti a second_stage lascia il sistema senza policy: puo' bastare,
# perche' a servicemanager serve che selinuxfs esista, ma non e' il percorso
# previsto. `winq.selinux=skip` sulla riga di comando forza la scorciatoia, per
# distinguere i due casi quando qualcosa non va.
STAGE=second_stage
if grep -qw 'winq.selinux=skip' /proc/cmdline 2>/dev/null; then
    echo "=== winq: selinux_setup saltato su richiesta" > /dev/kmsg
elif mount -t selinuxfs selinuxfs /sys/fs/selinux 2>/dev/null; then
    echo "=== winq: selinuxfs montato, passo per selinux_setup" > /dev/kmsg
    STAGE=selinux_setup
else
    echo "=== winq: nessun selinuxfs (kernel senza SELinux), vado diretto" > /dev/kmsg
fi

echo "=== winq: initramfs avviato" > /dev/kmsg

# --- radice di Android -------------------------------------------------------
# system.img e' system-as-root: contiene direttamente /, con /system dentro.
mount -o ro /dev/vda /android || { echo "WINQ-FERMO: system.img non montabile" > /dev/kmsg; exec sh; }

# --- vendor come overlay -----------------------------------------------------
mkdir -p /vendor_lower /vo
mount -o ro /dev/vdb /vendor_lower || { echo "WINQ-FERMO: vendor.img non montabile" > /dev/kmsg; exec sh; }
# upperdir e workdir devono stare sullo stesso filesystem, e quel filesystem
# deve reggere gli xattr: il tmpfs li ha (CONFIG_TMPFS_XATTR nel kernel guest).
mount -t tmpfs -o mode=0755 tmpfs /vo
mkdir -p /vo/upper/lib64/hw /vo/work

# Il compositore DRM, che e' il motivo di tutto questo.
cp /winq/hwcomposer.drm.so /vo/upper/lib64/hw/hwcomposer.drm.so
chmod 0644 /vo/upper/lib64/hw/hwcomposer.drm.so

# L'HAL audio con l'anello da 4096 frame, nella STESSA cartella e con lo stesso
# meccanismo del compositore qui sopra.
#
# Stava dentro vendor.img fino al, ed era l'ultima immagine del
# progetto che non fosse vergine. Spostarlo qui e' cio' che permette di
# scaricare vendor.img da monte invece di redistribuirla: il file pesa 19 KB,
# l'immagine 241 MB.
#
# NON e' una patch di due byte, per quanto il copione installa-hal-4096.sh lo
# dica nel commento in cima: misurato estraendo il.so dalle due
# immagini, sono 14.296 byte diversi su dimensioni diverse (19.880 contro
# 19.664). Il .so e' RICOSTRUITO, e per questo si spedisce invece di rifarlo
# all'avvio.
#
# E' LEGATO ALLA BUILD DEL VENDOR da cui e' stato ricavato: se l'immagine a
# monte si aggiorna, questo binario va ri-derivato. Sovrapporre un HAL costruito
# per un'altra build rompe l'audio in silenzio.
#
# Se manca, si prosegue con l'HAL di serie invece di fermare l'avvio: l'anello
# torna a 1024 frame e lo stutter sotto carico si ripresenta, ma un guest che
# suona male e' meglio di un guest che non parte. Il costruttore dell'initramfs
# si ferma comunque prima, quindi arrivare qui senza il file vuol dire che
# qualcuno ha composto l'initramfs a mano.
if [ -f /winq/audio.primary.waydroid.so ]; then
    cp /winq/audio.primary.waydroid.so /vo/upper/lib64/hw/audio.primary.waydroid.so
    chmod 0644 /vo/upper/lib64/hw/audio.primary.waydroid.so
else
    echo "=== winq: ATTENZIONE HAL audio assente: anello a 1024 frame, stutter atteso" > /dev/kmsg
fi

# Imposta una proprieta' in un build.prop: SOSTITUISCE la riga se c'e' gia',
# la aggiunge in coda solo se manca davvero.
#
# Serve perche' per le ro.* vince la PRIMA assegnazione, e il modo sbagliato non
# si vede: accodare "ro.product.locale=en-US" a un file che gia' contiene
# "ro.product.locale=it-IT" produce un file piu' lungo e nessun cambiamento,
# senza un errore da nessuna parte. Il file cambia, la proprieta' no.
# Le righe fisse di /winq/vendor-props restano invece in coda perche' quelle
# proprieta' nell'immagine non ci sono: li' l'append e' corretto.
imposta_prop() {
    prop_file=$1
    prop_chiave=$2
    prop_valore=$3
    # I punti della chiave vanno protetti, altrimenti in regex valgono
    # "qualunque carattere" e la riga giusta e' solo una delle possibili.
    prop_fuga=$(printf '%s' "$prop_chiave" | sed 's/\./\\./g')
    if grep -q "^$prop_fuga=" "$prop_file"; then
        sed -i "s|^$prop_fuga=.*|$prop_chiave=$prop_valore|" "$prop_file"
    else
        printf '%s=%s\n' "$prop_chiave" "$prop_valore" >> "$prop_file"
        echo "=== winq: $prop_chiave non c'era, aggiunta in coda" > /dev/kmsg
    fi
}

# build.prop: copia dell'originale piu' le nostre righe in coda. Per le ro.*
# vince la PRIMA assegnazione — sono di sola lettura, e i tentativi successivi
# vengono rifiutati — quindi aggiungere in coda funziona solo per proprieta' che
# sopra non ci sono. Le nostre non ci sono: verificato, ro.hardware.hwcomposer
# non e' impostata da nessuna parte nell'immagine.
cp /vendor_lower/build.prop /vo/upper/build.prop
cat /winq/vendor-props >> /vo/upper/build.prop

# Sovrascritture prese dalla riga di comando del kernel, nella forma
#   winq.prop.ro.hardware.gralloc=minigbm_gbm_mesa
# Servono per provare varianti senza ricostruire l'initramfs: si cambia una parola
# nello scenario e si riavvia. Vanno **prima** delle righe fisse, perche' per le
# ro.* vince la prima assegnazione.
for arg in $(cat /proc/cmdline); do
    case "$arg" in
        winq.prop.*=*)
            coppia=${arg#winq.prop.}
            printf '%s\n' "$coppia" > /tmp/winq-override
            cat /vo/upper/build.prop >> /tmp/winq-override
            mv /tmp/winq-override /vo/upper/build.prop
            echo "=== winq: sovrascrittura da cmdline: $coppia" > /dev/kmsg
            ;;
    esac
done
chmod 0644 /vo/upper/build.prop

# Verifica esplicita: quali righe finiscono davvero in coda alla build.prop. Serve
# perche' dedurre l'effetto delle proprieta' dal comportamento di Android e' lento
# e sbagliato — meglio leggere il file che init leggera'.
echo "=== winq: coda della build.prop del vendor" > /dev/kmsg
grep -E '^ro\.hardware|^ro\.vndk\.lite|^ro\.opengles' /vo/upper/build.prop > /dev/kmsg 2>&1

# --- il nome del prodotto: sta in odm, non in system -------------------------
# Il nome mostrato da Android non viene da /system/build.prop. init deriva
# ro.product.model dalle varianti per partizione seguendo
# ro.product.property_source_order, e qui vince odm. Il log lo dice a chiare
# lettere:
#   init: Setting product property ro.product.model to 'WayDroid arm64 only
#         Device' (from ro.product.odm.model)
# Nella radice di Android /odm e' un symlink verso il vendor, e dentro
# vendor.img /odm/etc e' una directory vera (inode 455): quindi il file da
# coprire e' /vendor_lower/odm/etc/build.prop e basta l'overlay del vendor che
# gia' esiste, senza toccare l'immagine.
#
# SOSTITUZIONE e non append: le quattro ro.product.odm.* ci sono gia' in quel
# file, e per le ro.* vince la prima assegnazione. Un append qui sarebbe un
# non-fare che sembra un fare.
if [ -f /vendor_lower/odm/etc/build.prop ]; then
    mkdir -p /vo/upper/odm/etc
    cp /vendor_lower/odm/etc/build.prop /vo/upper/odm/etc/build.prop
    imposta_prop /vo/upper/odm/etc/build.prop ro.product.odm.model        GodziDroid
    imposta_prop /vo/upper/odm/etc/build.prop ro.product.odm.name         GodziDroid
    imposta_prop /vo/upper/odm/etc/build.prop ro.product.odm.brand        GodziDroid
    imposta_prop /vo/upper/odm/etc/build.prop ro.product.odm.manufacturer GodziDroid
    chmod 0644 /vo/upper/odm/etc/build.prop
    echo "=== winq: nome del prodotto in odm/etc/build.prop" > /dev/kmsg
    grep -E '^ro\.product\.odm\.(model|name|brand|manufacturer)=' /vo/upper/odm/etc/build.prop > /dev/kmsg 2>&1
else
    # Non fatale: il sistema avvia lo stesso, col nome di Waydroid. Ma va detto,
    # perche' altrimenti il nome vecchio sembrerebbe una sed che non ha preso.
    echo "=== winq: ATTENZIONE nessun /odm/etc/build.prop nel vendor, nome non impostato" > /dev/kmsg
fi

# Il fstab che vold pretende. L'overlay fonde le directory, quindi questo file
# si aggiunge a /vendor/etc senza nascondere il resto.
# Nessun IDC per il tablet: dichiararlo touchscreen lo rendeva uno schermo tattile
# morto, perche' QEMU non gli fa mai emettere BTN_TOUCH. Lasciato come mouse, offre
# un cursore utile; il tocco arriva dal dispositivo multitouch, che Android
# riconosce da solo grazie agli assi ABS_MT.

# Il tablet come puntatore: senza questo Android lo prende per un touchscreen,
# perche' dichiara BTN_TOUCH, e attende un contatto che QEMU non invia mai.
mkdir -p /vo/upper/usr/idc
cp /winq/QEMU_Virtio_Tablet.idc /vo/upper/usr/idc/QEMU_Virtio_Tablet.idc
chmod 0644 /vo/upper/usr/idc/QEMU_Virtio_Tablet.idc

mkdir -p /vo/upper/etc
cp /winq/fstab.waydroid /vo/upper/etc/fstab.waydroid
chmod 0644 /vo/upper/etc/fstab.waydroid

# --- il manifest VINTF senza gli HAL di Waydroid ----------------------------
# Il SurfaceFlinger di queste immagini e' patchato da Waydroid: nel costruttore
# di HidlComposer attende vendor.waydroid.display@1.0::IWaydroidDisplay, e quel
# servizio lo pubblica hwcomposer.waydroid.so — cioe' proprio il compositore che
# stiamo sostituendo. Il backtrace lo mostra senza ambiguita':
#   getRawServiceInternal -> vendor.waydroid.display@1.0.so
#   -> Hwc2::HidlComposer::HidlComposer -> HWComposer -> SurfaceFlinger::init
# Risultato: init() non ritorna mai e SurfaceFlinger non pubblica nulla.
#
# getService attende perche' l'interfaccia e' **dichiarata** nel manifest del
# vendor. Togliendo la dichiarazione l'attesa non ha piu' ragione: se il codice
# patchato regge un puntatore nullo si prosegue, altrimenti si vede un crash
# chiaro invece di un blocco muto. E' un esperimento, non una soluzione: la via
# pulita e' un system.img il cui SurfaceFlinger non sia patchato.
if [ -f /vendor_lower/etc/vintf/manifest.xml ] && ! grep -qw 'winq.keep_waydroid_hal' /proc/cmdline 2>/dev/null; then
    mkdir -p /vo/upper/etc/vintf
    # winq.no_mapper4 toglie anche graphics.mapper, e serve a un esperimento
    # preciso: drm_hwcomposer scegle il suo BufferInfoGetter una volta sola —
    # BufferInfoMapperMetadata se il servizio IMapper 4 esiste, altrimenti
    # BufferInfoLibdrm — e non ripiega se poi la conversione fallisce. Il vendor ha
    # un solo mapper 4.0, quello di minigbm_gbm_mesa, che e' un fork: se non
    # espone i metadati standard dei piani si ottiene "Failed to import buffer" per
    # sempre. Rendendolo indisponibile si forza la via legacy, che con
    # gralloc.gbm.so (gbm_gralloc, handle gralloc_handle_t) e' coerente.
    # SOLO vendor.waydroid.display, non tutti i vendor.waydroid.*: togliere anche
    # task e window impedisce a quei servizi di registrarsi —
    #   Service vendor.waydroid.task@1.0::IWaydroidTask/default must be in VINTF
    #   manifest in order to register/get
    # — e task-hal-1-0 si riavvia ogni cinque secondi per sempre, tenendo il
    # framework indietro. Serve solo che il display NON sia dichiarato, perche' e'
    # quello che SurfaceFlinger attende.
    if grep -qw 'winq.no_mapper4' /proc/cmdline 2>/dev/null; then
        filtro='vendor\.waydroid\.display|graphics\.mapper'
    else
        filtro='vendor\.waydroid\.display'
    fi
    awk -v filtro="$filtro" '
        /<hal/          { buf = $0; dentro = 1; next }
        dentro          { buf = buf "\n" $0
                          if (/<\/hal>/) {
                              if (buf !~ filtro) print buf
                              dentro = 0
                          }
                          next }
                        { print }
    ' /vendor_lower/etc/vintf/manifest.xml > /vo/upper/etc/vintf/manifest.xml
    chmod 0644 /vo/upper/etc/vintf/manifest.xml
    prima=$(grep -c '<hal' /vendor_lower/etc/vintf/manifest.xml)
    dopo=$(grep -c '<hal' /vo/upper/etc/vintf/manifest.xml)
    echo "=== winq: manifest VINTF, blocchi hal $prima -> $dopo" > /dev/kmsg
fi

# Diagnostica: se il file c'e' nell'initramfs, lo installiamo. Assente, l'avvio
# procede identico — cosi' si accende e si spegne senza toccare altro.
if [ -f /winq/winq-logcat.rc ]; then
    mkdir -p /vo/upper/etc/init
    cp /winq/winq-logcat.rc /vo/upper/etc/init/winq-logcat.rc
    cp /winq/winq-logcat.sh /vo/upper/etc/winq-logcat.sh
    chmod 0644 /vo/upper/etc/init/winq-logcat.rc
    chmod 0755 /vo/upper/etc/winq-logcat.sh
fi
if [ -f /winq/winq-probe.rc ]; then
    mkdir -p /vo/upper/etc/init
    cp /winq/winq-probe.rc /vo/upper/etc/init/winq-probe.rc
    cp /winq/winq-probe.sh /vo/upper/etc/winq-probe.sh
    chmod 0644 /vo/upper/etc/init/winq-probe.rc
    chmod 0755 /vo/upper/etc/winq-probe.sh
fi

mount -t overlay overlay \
    -o lowerdir=/vendor_lower,upperdir=/vo/upper,workdir=/vo/work \
    /android/vendor || { echo "WINQ-FERMO: overlay su /vendor fallito" > /dev/kmsg; exec sh; }

# --- system come overlay di SOLA LETTURA -------------------------------------
# Un solo montaggio per tre personalizzazioni, perche' system.img non si tocca:
# deve restare l'immagine vergine che l'utente scarica.
#
#   1. l'APK degli appunti come app di SISTEMA. `pm install` non basta: un'app
#      appena installata resta in stato "stopped" finche' l'utente non la apre a
#      mano, e un'app stopped non riceve BOOT_COMPLETED: misurato, il processo
#      non esiste proprio. Ne' si puo' rimediare dalla shell, perche'
#      BOOT_COMPLETED e' protetta e non la si puo' inviare con `am broadcast`.
#      Un'app di sistema non e' mai stopped: il receiver scatta al boot.
#   2. l'animazione d'avvio. Va in /system/product/media, NON in /system/media:
#      quella directory nell'immagine non esiste (verificato), e scriverci
#      dentro darebbe un overlay che non copre nulla e un'animazione invariata.
#   3. la lingua in /system/build.prop.
#
# SOLA LETTURA per costruzione: niente upperdir, niente workdir. Overlayfs
# rifiuta ogni scrittura e nessun copy-up e' possibile, quindi /system resta
# immutabile esattamente come quando era montato ro.
#
# Nella lista dei lowerdir vince il PRIMO: /so/extra sta davanti a
# /system_lower, quindi i nostri file coprono gli omonimi dell'immagine mentre
# tutto il resto continua a vedersi (le directory si fondono).
#
# DEBITO, non una scelta: i file che arrivano dal tmpfs non hanno le etichette
# SELinux che avrebbero dentro system.img (system_file per l'APK,
# system_build_prop per la build.prop). Passa solo perche' qui SELinux gira
# permissive. In enforcing questo blocco va rifatto (mount con -o context= o
# etichettatura per file), altrimenti non e' l'APK a non partire: e' zygote a
# non poterlo leggere.
mkdir -p /system_lower /so
# Il bind da' al contenuto dell'immagine un secondo nome, stabile. Serve perche'
# fra poche righe l'overlay si monta proprio su /android/system: passare come
# lowerdir lo stesso percorso su cui si sta montando e' un riferimento circolare,
# e dopo il montaggio quel percorso mostrerebbe l'overlay, non l'immagine. Con il
# bind il lower ha un nome suo: la copia della build.prop qui sotto legge
# l'originale con certezza, e resta un modo per confrontare immagine e risultato.
mount --bind /android/system /system_lower || { echo "WINQ-FERMO: bind di /system fallito" > /dev/kmsg; exec sh; }
mount -t tmpfs -o mode=0755 tmpfs /so
mkdir -p /so/extra

if [ -f /winq/HabumiClipboard.apk ]; then
    mkdir -p /so/extra/app/HabumiClipboard
    cp /winq/HabumiClipboard.apk /so/extra/app/HabumiClipboard/HabumiClipboard.apk
    chmod 0755 /so/extra/app /so/extra/app/HabumiClipboard
    chmod 0644 /so/extra/app/HabumiClipboard/HabumiClipboard.apk
else
    echo "=== winq: ATTENZIONE APK degli appunti assente dall'initramfs" > /dev/kmsg
fi

if [ -f /winq/bootanimation.zip ]; then
    mkdir -p /so/extra/product/media
    cp /winq/bootanimation.zip /so/extra/product/media/bootanimation.zip
    chmod 0755 /so/extra/product /so/extra/product/media
    chmod 0644 /so/extra/product/media/bootanimation.zip
else
    echo "=== winq: ATTENZIONE bootanimation.zip assente dall'initramfs" > /dev/kmsg
fi

# asound.conf, ED E' CIO' CHE FA SUONARE L'AUDIO.
#
# PERCHE' STA QUI E NON DENTRO L'IMMAGINE. Fino al questo file era
# stato SCRITTO dentro system.img a mano, e per questo l'immagine non era piu'
# quella pubblicata da Waydroid. Da qui in poi le immagini si vogliono VERGINI,
# perche' l'utente se le scarica da monte e noi non le redistribuiamo: quindi
# tutto cio' che il progetto aggiunge passa da questo overlay.
#
# Senza questo file l'HAL non trova il dispositivo ALSA e il guest resta muto.
# Il perche' dei tre nomi coperti, e il tentativo scartato di fissare
# period_size e buffer_size, stanno nei commenti dentro il file stesso.
if [ -f /winq/asound.conf ]; then
    mkdir -p /so/extra/etc
    cp /winq/asound.conf /so/extra/etc/asound.conf
    chmod 0755 /so/extra/etc
    chmod 0644 /so/extra/etc/asound.conf
else
    echo "=== winq: ATTENZIONE asound.conf assente: il guest restera' muto" > /dev/kmsg
fi

# La lingua. imposta_prop sostituisce la riga se c'e' e la aggiunge se manca:
# nell'immagine attuale ro.product.locale c'e' gia', quindi accodarla non
# avrebbe alcun effetto (vince la prima assegnazione), ma il caso "manca" va
# coperto lo stesso perche' un'altra immagine potrebbe non averla.
cp /system_lower/build.prop /so/extra/build.prop
imposta_prop /so/extra/build.prop ro.product.locale en-US
chmod 0644 /so/extra/build.prop
grep -E '^ro\.product\.locale=' /so/extra/build.prop > /dev/kmsg 2>&1

# Non fatale di proposito: se l'overlay non si monta, Android avvia comunque
# senza le tre personalizzazioni, e da li' si diagnostica con adb. Fermarsi in
# una shell dell'initramfs toglierebbe l'unico canale utile.
if mount -t overlay overlay -o lowerdir=/so/extra:/system_lower /android/system; then
    echo "=== winq: overlay su /system montato (appunti, animazione, lingua)" > /dev/kmsg
    ls -l /android/system/app/HabumiClipboard /android/system/product/media/bootanimation.zip 2>&1 | while read -r l; do echo "=== winq: system $l" > /dev/kmsg; done
else
    echo "=== winq: ATTENZIONE overlay su /system fallito, avvio senza personalizzazioni" > /dev/kmsg
fi

# --- dati --------------------------------------------------------------------
mount -o rw /dev/vdc /android/data || { echo "WINQ-FERMO: data.img non montabile" > /dev/kmsg; exec sh; }
mkdir -p /android/data/local/tmp
chmod 0771 /android/data

# Anche questi li monta la prima fase: /mnt e' un tmpfs con gid 1000, e dentro
# ci vanno le due directory su cui piu' tardi vengono innestate le partizioni
# APEX del vendor. Vanno creati nella radice di Android, non nell'initramfs.
mount -t tmpfs -o mode=0755,uid=0,gid=1000 tmpfs /android/mnt
mkdir -p /android/mnt/vendor /android/mnt/product

# /linkerconfig deve essere un tmpfs scrivibile. E' il tassello che tiene in piedi
# tutto il resto: init esegue `linkerconfig`, che vi scrive ld.config.txt, cioe' la
# mappa che dice al linker dove stanno le librerie fornite dagli APEX. Senza quel
# file il linker non trova nulla di cio' che vive in /apex, e ogni processo che ne
# dipende muore:
#   linker: failed to find generated linker configuration from
#           "/linkerconfig/ld.config.txt"
#   CANNOT LINK EXECUTABLE "/system/bin/app_process64":
#           library "libnativeloader.so" not found
# Cadevano cosi' zygote, netd, keystore2, audioserver, statsd e gpuservice, ognuno
# lamentando una libreria diversa — sei sintomi diversi, una causa sola.
mount -t tmpfs -o mode=0755,uid=0,gid=0 tmpfs /android/linkerconfig

# /metadata e' una partizione a se' su un dispositivo vero, e init ci scrive
# durante post-fs. Un tmpfs basta per una VM usa e getta.
mkdir -p /android/metadata
mount -t tmpfs -o mode=0771,uid=0,gid=1000 tmpfs /android/metadata

echo "=== winq: partizioni pronte, passo a init second_stage" > /dev/kmsg

# I mount di servizio vanno spostati nella nuova radice: switch_root non li
# porta con se', e la seconda fase di init si aspetta /proc, /sys e /dev
# gia' montati (nel percorso normale li monta la prima fase).
mount --move /proc /android/proc
mount --move /sys  /android/sys
mount --move /dev  /android/dev

exec switch_root /android /system/bin/init "$STAGE"
