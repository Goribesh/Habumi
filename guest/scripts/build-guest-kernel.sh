#!/bin/sh
# Costruisce il kernel del guest: Linux ARM64 per QEMU -M virt, con quanto
# serve ad Android.
#
# Va eseguito dentro la distro WSL, che ha la toolchain aarch64 nativa. Il
# risultato e' un singolo file Image da copiare in guest/images/.
#
# Perche' non si riusa il kernel della sessione precedente: quello e' l'albero
# Microsoft per WSL2 e su QEMU panica immediatamente con "Oops - Undefined
# instruction" — dipende dall'ambiente che l'hypervisor di WSL gli prepara.
# Il config di quel kernel resta comunque utile come riferimento: aveva gia'
# binder, binderfs e i driver virtio giusti.
#
# Due scelte non ovvie:
#
#  - i driver virtio sono compilati **dentro** il kernel, non come moduli.
#    Come moduli servirebbe un initramfs solo per montare il disco, e in
#    questa fase e' complessita' senza scopo.
#  - DEBUG_INFO_BTF resta spento. Con gcc 15 la generazione BTF fallisce
#    (pahole non regge il DWARF prodotto), ed e' il problema che aveva gia'
#    fermato la build nella sessione precedente. Nulla di cio' che serve qui
#    dipende da BTF.

set -e

# Il percorso degli script si risolve QUI, prima di qualunque cd: piu' sotto si
# entra nell'albero del kernel, e da la' un "dirname $0" relativo punta altrove.
SCRIPTDIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="${KERNEL_VERSION:-6.18.35}"
SRC="$HOME/linux-$VERSION"
JOBS="${JOBS:-$(nproc)}"

echo "=== kernel $VERSION, $JOBS job paralleli"

if [ ! -d "$SRC" ]; then
    cd "$HOME"
    TARBALL="linux-$VERSION.tar.xz"
    if [ ! -f "$TARBALL" ]; then
        echo "=== scarico $TARBALL"
        MAJOR=$(echo "$VERSION" | cut -d. -f1)
        curl -fL -o "$TARBALL" \
            "https://cdn.kernel.org/pub/linux/kernel/v${MAJOR}.x/$TARBALL"
    fi
    echo "=== estraggo"
    tar -xf "$TARBALL"
fi

cd "$SRC"

# --- LA PATCH DEI FORMATI DI ANDROID, PRIMA DELLA CONFIGURAZIONE.
#
# Insegna a virtio-gpu i formati con cui Android compone: RGBX_8888 e RGBA_8888
# diventano in DRM XBGR8888 e ABGR8888, e il driver accettava solo HOST_XRGB8888,
# rifiutando gli altri con ENOENT. Il risultato e' drm_hwcomposer che chiama
# drmModeAddFB2, il kernel che risponde ENOENT, e sullo schermo la console di avvio
# invece dell'interfaccia -- oppure, con QEMU, la finestra che dice "Display output
# is not active".
#
# PERCHE' STA QUI E NON A MANO. Per una sessione intera questa modifica era stata
# applicata a mano all'albero, e guest/patches/kernel-virtio-gpu-formati-android.patch
# ne era solo la DOCUMENTAZIONE: "patch: Only garbage was found in the patch input".
# Conseguenza: il kernel che funzionava esisteva come file binario e NESSUNO poteva
# ricostruirlo. Se ne e' avuta la prova nel modo peggiore -- due ricompilazioni di
# fila che avviavano Android senza mostrare niente, e la causa era questa modifica
# assente. Chiamarla da qui e' cio' che rende il kernel riproducibile dal repo.
#
# Va prima di make defconfig perche' toccando i sorgenti del driver deve essere in
# posto quando si compila, e perche' fallire qui costa un secondo invece di
# trenta minuti.
PATCH_FORMATI="$SCRIPTDIR/patch-kernel-virtio-gpu.sh"
if [ ! -f "$PATCH_FORMATI" ]; then
    echo "FERMO: manca $PATCH_FORMATI, e senza quella modifica Android"
    echo "       avvia senza mostrare nulla. Vedi guest/patches/."
    exit 1
fi
sh "$PATCH_FORMATI" "$SRC"

# Stessa ragione della precedente: il driver virtio-snd smette di spedire audio
# dopo il primo ciclo stop/prepare e non riparte piu'. La modifica sta nei
# sorgenti, quindi deve essere in posto prima di compilare, e se manca l'audio
# si inchioda dopo pochi secondi di riproduzione. Il perche' e' scritto per
# esteso in testa allo script.
PATCH_SUONO="$SCRIPTDIR/patch-kernel-virtio-snd.sh"
if [ ! -f "$PATCH_SUONO" ]; then
    echo "FERMO: manca $PATCH_SUONO, e senza quella modifica l'audio si"
    echo "       ferma dopo pochi secondi e non riparte."
    exit 1
fi
sh "$PATCH_SUONO" "$SRC"

echo "=== configurazione di base"
make ARCH=arm64 defconfig

# `defconfig` per arm64 include gia' PCI_HOST_GENERIC, il PL011 e virtio come
# moduli. Qui si aggiunge cio' che manca e si promuove a builtin quel che
# serve al boot.
echo "=== opzioni per QEMU virt e Android"
cat > .config-extra <<'EOF'
# --- virtio, tutto builtin: nessun initramfs per montare il disco
CONFIG_VIRTIO=y
CONFIG_VIRTIO_PCI=y
CONFIG_VIRTIO_MMIO=y
CONFIG_VIRTIO_BLK=y
CONFIG_VIRTIO_NET=y
CONFIG_VIRTIO_CONSOLE=y
CONFIG_VIRTIO_INPUT=y
CONFIG_VIRTIO_BALLOON=y

# --- virtio-gpu: il motivo per cui esiste tutto il progetto
CONFIG_DRM=y
CONFIG_DRM_VIRTIO_GPU=y
CONFIG_DRM_VIRTIO_GPU_KMS=y
CONFIG_DRM_FBDEV_EMULATION=y

# --- AUDIO: virtio-snd, e il motivo per cui questi quattro simboli stanno qui.
#
# MISURATO : nel guest non esisteva /dev/snd, e la causa era tutta
# nella configurazione -- defconfig da' SOUND=m, SND=m, SND_PCM=m, e
# CONFIG_SND_VIRTIO non compariva affatto. Senza quei nodi l'HAL audio di Android
# non ha niente da aprire, e il resto della catena (che c'e' tutto: libasound,
# audio.primary.waydroid.so con playback E capture, la policy, i permessi in
# ueventd) resta inerte senza dire perche'.
#
# BUILTIN E NON MODULI, per la ragione che questo file documenta gia' tre volte
# (BRIDGE, le tabelle legacy, IP6_NF_IPTABLES): si compila solo Image, quindi un
# simbolo a "m" e' per noi identico a un simbolo assente, e il guasto che ne
# segue non nomina la causa. SND_PCM e' esplicito anche se SND_VIRTIO lo
# tirerebbe: il valore di questo elenco e' dire cosa serve, non far dedurre.
#
# Il microfono non chiede nulla in piu' qui: viene dal lato QEMU, dove
# VIRTIO_SOUND_STREAM_DEFAULT vale 2 e lo stream 0 e' uscita, l'1 ingresso.
CONFIG_SOUND=y
CONFIG_SND=y
CONFIG_SND_PCM=y
CONFIG_SND_VIRTIO=y

# --- Android: IPC e il filesystem dei device binder
CONFIG_ANDROID_BINDER_IPC=y
CONFIG_ANDROID_BINDERFS=y
CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"

# --- Android: buffer condivisi e sincronizzazione
CONFIG_DMABUF_HEAPS=y
CONFIG_DMABUF_HEAPS_SYSTEM=y
CONFIG_DMABUF_HEAPS_CMA=y
CONFIG_SYNC_FILE=y
CONFIG_SW_SYNC=y
CONFIG_UDMABUF=y

# --- contenitori: Waydroid gira Android in un container LXC
CONFIG_NAMESPACES=y
CONFIG_UTS_NS=y
CONFIG_IPC_NS=y
CONFIG_USER_NS=y
CONFIG_PID_NS=y
CONFIG_NET_NS=y
CONFIG_CGROUPS=y
CONFIG_CGROUP_DEVICE=y
CONFIG_CGROUP_FREEZER=y
CONFIG_CGROUP_SCHED=y
CONFIG_MEMCG=y
# I controller cgroup **v1**, e sono il punto che ha bloccato Android piu' a
# lungo di ogni altro. Dal 6.11 i controller v1 stanno dietro simboli propri e
# il defconfig li lascia spenti, perche' il mondo si e' spostato su v2. Ma
# libprocessgroup di Android monta ancora le gerarchie v1: senza questi due, il
# kernel risponde "cgroup: Unknown subsys name 'memory'" e ogni servizio muore
# su createProcessGroup. In WSL funzionava perche' quel kernel li aveva.
#
# Non basta abilitarli sull'host: systemd 259 ha rimosso il supporto v1, quindi
# systemd.unified_cgroup_hierarchy=0 viene ignorato. E' Android, dentro il
# container, che monta le proprie gerarchie — e per farlo il kernel deve
# conoscerle.
CONFIG_MEMCG_V1=y
CONFIG_CPUSETS_V1=y

# PSI, la misura della pressione su CPU, memoria e I/O. Android 13 la pretende:
# lmkd la usa per decidere quando uccidere processi, e senza di essa **esce con
# stato 0** — pulito, non in errore. init conta quelle uscite e alla quarta,
# essendo lmkd un processo critico, si uccide:
#   init: Service 'lmkd' (pid 296) exited with status 0
#   init: critical process 'lmkd' exited 4 times before boot completed
#   init: InitFatalReboot: signal 6
# Nessuna di quelle righe nomina PSI, e il sistema riparte da capo in ciclo.
#
# PSI_DEFAULT_DISABLED va lasciata spenta: se attiva, PSI c'e' ma resta inerte
# finche' non si passa psi=1 sulla riga di comando, e lmkd si comporta come se
# mancasse.
CONFIG_PSI=y
# CONFIG_PSI_DEFAULT_DISABLED is not set
CONFIG_OVERLAY_FS=y
CONFIG_FUSE_FS=y
CONFIG_SQUASHFS=y
CONFIG_SQUASHFS_XZ=y
CONFIG_EXT4_FS=y
CONFIG_TMPFS_POSIX_ACL=y
CONFIG_TMPFS_XATTR=y

# --- rete per il container.
#
# Waydroid crea un bridge (waydroid0) e lo NATta verso l'esterno. Chiedere
# questi simboli non basta: il defconfig li tiene a modulo e merge_config non
# li promuove, e avendo compilato solo Image i moduli non esistono. Il guest
# risponde "ip link add type bridge: Unknown device type" e la sessione non
# parte mai. Le dipendenze sono elencate per esteso invece di lasciarle
# dedurre: NF_NAT non veniva abilitato da nessuno, e senza di lui IP_NF_NAT e
# il target MASQUERADE non esistono nemmeno come opzioni.
# IPV6 builtin, e non e' un dettaglio: BRIDGE dichiara
#   depends on IPV6 || IPV6=n
# e con IPV6=m quell'espressione vale m, che limita BRIDGE a modulo qualunque
# cosa si chieda. Con solo Image compilata il modulo non esiste, e Waydroid
# muore su "ip link add type bridge: Unknown device type" — una riga che non
# nomina ne' IPv6 ne' i moduli.
CONFIG_IPV6=y
CONFIG_BRIDGE=y
CONFIG_LLC=y
CONFIG_STP=y
CONFIG_BRIDGE_NETFILTER=y
CONFIG_VETH=y
CONFIG_TUN=y
CONFIG_MACVLAN=y
# ROUTING PER CRITERI: senza questo Android non ha rete oltre la propria sottorete.
# MISURATO nel guest: eth0 sale con 10.0.2.15/24, il gateway 10.0.2.2 e il DNS
# 10.0.2.3 rispondono al ping, ma 1.1.1.1 da' "Network is unreachable" e
# "ip route get 1.1.1.1" pure. Il sintomo che nomina la causa e' un altro:
#     ip rule show -> Operation not supported on transport endpoint
# cioe' il kernel non ha il routing per criteri.
#
# Android ne dipende in modo essenziale: netd installa la rotta di default in una
# tabella PER-RETE scelta da fwmark, non nella tabella principale. Senza il
# supporto, quella rotta non atterra in nessuna tabella utilizzabile -- e il
# risultato inganna, perche' "dumpsys connectivity" mostra la rotta fra le
# LinkProperties (contabilita' in spazio utente) mentre il kernel non l'ha, e le
# capacita' della rete restano senza VALIDATED perche' la sonda non esce.
#
# CONFIG_IP_MULTIPLE_TABLES dipende da CONFIG_IP_ADVANCED_ROUTER: servono entrambe.
CONFIG_IP_ADVANCED_ROUTER=y
CONFIG_IP_MULTIPLE_TABLES=y
CONFIG_NETFILTER=y
CONFIG_NETFILTER_ADVANCED=y
CONFIG_NF_CONNTRACK=y
CONFIG_NF_NAT=y
CONFIG_NF_NAT_MASQUERADE=y
CONFIG_NF_TABLES=y
CONFIG_NF_TABLES_INET=y
CONFIG_NFT_NAT=y
CONFIG_NFT_MASQ=y
CONFIG_NFT_CHAIN_NAT=y
CONFIG_NETFILTER_XTABLES=y
CONFIG_NETFILTER_XT_MATCH_COMMENT=y
CONFIG_NETFILTER_XT_MATCH_CONNTRACK=y
CONFIG_NETFILTER_XT_NAT=y
CONFIG_NETFILTER_XT_TARGET_MASQUERADE=y
# CHECKSUM serve davvero: waydroid-net.sh aggiunge una regola mangle
#   -j CHECKSUM --checksum-fill
# sul traffico DHCP verso il container. Senza il target, iptables risponde
# "Extension CHECKSUM revision 0 not supported, missing kernel module?" e lo
# script, che gira con set -e, aborta e smonta tutto quello che aveva creato —
# bridge compreso, per cui l'unica traccia visibile e' un waydroid0 che non
# esiste. Le estensioni sono state ricavate leggendo lo script invece di
# aggiungerne una per tentativo: usa ACCEPT, CHECKSUM, MASQUERADE e -m udp.
CONFIG_NETFILTER_XT_TARGET_CHECKSUM=y
CONFIG_IP_NF_MANGLE=y
# Le tabelle legacy servono davvero: waydroid-net.sh invoca iptables-legacy.
# In 6.18 sono dietro un simbolo nuovo, NETFILTER_XTABLES_LEGACY, e senza
# quello IP_NF_IPTABLES_LEGACY non esiste — e con esso nemmeno IP_NF_NAT, che
# ne dipende.
CONFIG_NETFILTER_XTABLES_LEGACY=y
CONFIG_IP_NF_IPTABLES=y
CONFIG_IP_NF_IPTABLES_LEGACY=y
CONFIG_IP_NF_FILTER=y
CONFIG_IP_NF_NAT=y
CONFIG_IP_NF_TARGET_MASQUERADE=y

# --- IL SOTTOINSIEME DI RETE CHE ANDROID DICHIARA OBBLIGATORIO.
#
# LA CAUSA, e non un sospetto. Lo schema con cui Android instrada per rete ha DUE
# meta': netd MARCHIA i pacchetti con fwmark tramite "-j MARK", e "ip rule"
# SELEZIONA la tabella di routing in base a quel marchio. La correzione precedente
# ha aggiunto IP_ADVANCED_ROUTER e IP_MULTIPLE_TABLES, cioe' la meta' che seleziona.
# Ma NETFILTER_XT_TARGET_MARK e NETFILTER_XT_MATCH_MARK mancavano entrambi, quindi
# netd non poteva marchiare nulla: nessun pacchetto corrispondeva alla regola che
# scegli la tabella dove sta la rotta di default.
#
# Da qui il sintomo che sembrava contraddittorio: "ip rule show" funzionava, le
# rotte erano installate, e il traffico non usciva comunque dalla sottorete. Era una
# serratura senza la sua chiave.
#
# DA DOVE VIENE QUESTO ELENCO. Non dai miei sospetti, che una volta hanno gia' dato
# una correzione necessaria e insufficiente. Viene da android-base.config del ramo
# android-5.15 di AOSP kernel/configs: il frammento che AOSP definisce come le
# impostazioni che DEVONO essere presenti perche' Android funzioni, distinto da
# android-recommended.config che contiene i miglioramenti non necessari.
# Confronto con la nostra configurazione: 260 richiesti, 114 conformi, 127 mancanti,
# 19 divergenti.
#
# PERCHE' SOLO IL SOTTOINSIEME DI RETE. Dei 127 mancanti, questi sono quelli su cui
# poggiano netd e il suo iptables-restore. Gli altri -- cifratura del filesystem,
# dm-verity, gadget USB, PPP, gamepad, e gli ASHMEM e UID_SYS_STATS che in mainline
# non esistono affatto -- non c'entrano con questo difetto, e abilitarli darebbe
# rischio di avvio in cambio di conformita' che nessuno esercita. Sono registrati
# come lacuna dichiarata invece di essere zittiti.
#
# PERCHE' ALCUNI SONO PROMOZIONI DA MODULO. Compiliamo solo Image, senza moduli
# installati: un simbolo a "m" per noi e' identico a un simbolo assente. Il difetto
# si e' presentato tre volte in questo progetto -- BRIDGE, le tabelle legacy, e ora
# IP6_NF_IPTABLES -- quindi vale come regola: qui dentro "m" non esiste.
#
# NETFILTER_XT_MATCH_QUOTA2 e QUOTA2_LOG sono esclusi: AOSP li richiede ma sono una
# patch fuori albero e in mainline 6.18 non esistono. Android li usa per le quote
# dei dati mobili, funzione che un emulatore su Wi-Fi non esercita.

# i match e i target di xtables che netd usa nelle proprie regole.
#     xt_owner e' il match per-UID su cui poggia il firewall per applicazione;
#     MARK e CONNMARK sono la meta' che MARCHIA i pacchetti, vedi sopra;
#     IDLETIMER e' il timer di inattivita' della rete; TPROXY e socket servono
#     al clatd e al proxy trasparente.
CONFIG_NETFILTER_XT_MATCH_BPF=y
CONFIG_NETFILTER_XT_MATCH_CONNLIMIT=y
CONFIG_NETFILTER_XT_MATCH_CONNMARK=y
CONFIG_NETFILTER_XT_MATCH_HASHLIMIT=y
CONFIG_NETFILTER_XT_MATCH_HELPER=y
CONFIG_NETFILTER_XT_MATCH_IPRANGE=y
CONFIG_NETFILTER_XT_MATCH_LENGTH=y
CONFIG_NETFILTER_XT_MATCH_LIMIT=y
CONFIG_NETFILTER_XT_MATCH_MAC=y
CONFIG_NETFILTER_XT_MATCH_MARK=y
CONFIG_NETFILTER_XT_MATCH_OWNER=y
CONFIG_NETFILTER_XT_MATCH_PKTTYPE=y
CONFIG_NETFILTER_XT_MATCH_POLICY=y
CONFIG_NETFILTER_XT_MATCH_QUOTA=y
CONFIG_NETFILTER_XT_MATCH_SOCKET=y
CONFIG_NETFILTER_XT_MATCH_STATE=y
CONFIG_NETFILTER_XT_MATCH_STATISTIC=y
CONFIG_NETFILTER_XT_MATCH_STRING=y
CONFIG_NETFILTER_XT_MATCH_TIME=y
CONFIG_NETFILTER_XT_MATCH_U32=y
CONFIG_NETFILTER_XT_TARGET_CLASSIFY=y
CONFIG_NETFILTER_XT_TARGET_CONNMARK=y
CONFIG_NETFILTER_XT_TARGET_CONNSECMARK=y
CONFIG_NETFILTER_XT_TARGET_CT=y
CONFIG_NETFILTER_XT_TARGET_IDLETIMER=y
CONFIG_NETFILTER_XT_TARGET_MARK=y
CONFIG_NETFILTER_XT_TARGET_NFLOG=y
CONFIG_NETFILTER_XT_TARGET_NFQUEUE=y
CONFIG_NETFILTER_XT_TARGET_SECMARK=y
CONFIG_NETFILTER_XT_TARGET_TCPMSS=y
CONFIG_NETFILTER_XT_TARGET_TPROXY=y
CONFIG_NETFILTER_XT_TARGET_TRACE=y

# gli helper di conntrack e il lookup dei socket: NF_SOCKET_IPV4 e IPV6 sono
#     le dipendenze del match socket, e senza di loro quel match non esiste
#     nemmeno come opzione.
CONFIG_NF_CONNTRACK_AMANDA=y
CONFIG_NF_CONNTRACK_FTP=y
CONFIG_NF_CONNTRACK_H323=y
CONFIG_NF_CONNTRACK_IRC=y
CONFIG_NF_CONNTRACK_NETBIOS_NS=y
CONFIG_NF_CONNTRACK_PPTP=y
CONFIG_NF_CONNTRACK_SANE=y
CONFIG_NF_CONNTRACK_SECMARK=y
CONFIG_NF_CONNTRACK_TFTP=y
CONFIG_NF_CT_NETLINK=y
CONFIG_NF_CT_PROTO_DCCP=y
CONFIG_NF_SOCKET_IPV4=y
CONFIG_NF_SOCKET_IPV6=y

# le tabelle IPv4 che netd popola oltre a nat e mangle: raw, security, e i
#     target REJECT, NETMAP, REDIRECT che compaiono nelle sue catene.
CONFIG_IP_NF_ARPFILTER=y
CONFIG_IP_NF_ARPTABLES=y
CONFIG_IP_NF_ARP_MANGLE=y
CONFIG_IP_NF_MATCH_ECN=y
CONFIG_IP_NF_MATCH_TTL=y
CONFIG_IP_NF_RAW=y
CONFIG_IP_NF_SECURITY=y
CONFIG_IP_NF_TARGET_NETMAP=y
CONFIG_IP_NF_TARGET_REDIRECT=y
CONFIG_IP_NF_TARGET_REJECT=y

# l'IPv6, che Android non tratta come opzionale. IPV6_MULTIPLE_TABLES e' il
#     gemello di IP_MULTIPLE_TABLES: senza di lui il routing per rete vale solo v4.
CONFIG_INET6_ESP=y
CONFIG_INET6_IPCOMP=y
CONFIG_IP6_NF_MATCH_RPFILTER=y
CONFIG_IP6_NF_RAW=y
CONFIG_IP6_NF_TARGET_REJECT=y
CONFIG_IPV6_MIP6=y
CONFIG_IPV6_MULTIPLE_TABLES=y
CONFIG_IPV6_OPTIMISTIC_DAD=y
CONFIG_IPV6_ROUTER_PREF=y
CONFIG_IPV6_ROUTE_INFO=y
CONFIG_IPV6_VTI=y
CONFIG_IP6_NF_FILTER=y
CONFIG_IP6_NF_IPTABLES=y
#     IP6_NF_IPTABLES_LEGACY non compare in android-base.config perche' nel 5.15 di
#     AOSP quella separazione non esisteva ancora. In 6.18 tutte le tabelle IPv6
#     legacy -- FILTER, MANGLE, RAW, SECURITY -- dichiarano "depends on
#     IP6_NF_IPTABLES_LEGACY", quindi a modulo esso trascina a modulo ogni figlio
#     qualunque cosa si chieda. E' la lezione di IP_NF_IPTABLES_LEGACY ripetuta
#     identica su IPv6, e l'ha trovata la verifica dello script invece di un avvio.
CONFIG_IP6_NF_IPTABLES_LEGACY=y
CONFIG_IP6_NF_MANGLE=y

# i diag socket: netd chiama SOCK_DESTROY per chiudere le connessioni quando
#     una rete cade, e senza INET_DIAG_DESTROY quella chiamata fallisce.
CONFIG_INET_DIAG_DESTROY=y
CONFIG_INET_ESP=y
CONFIG_INET_UDP_DIAG=y

# XFRM, cioe' IPsec: la VPN e il traffico marcato per interfaccia.
CONFIG_NET_IPGRE_DEMUX=y
CONFIG_NET_IPVTI=y
CONFIG_NET_KEY=y
CONFIG_XFRM_INTERFACE=y
CONFIG_XFRM_MIGRATE=y
CONFIG_XFRM_STATISTICS=y
CONFIG_XFRM_USER=y

# il traffic control con classificatore BPF: e' il percorso con cui netd
#     contabilizza il traffico per applicazione.
CONFIG_NET_CLS_BPF=y
CONFIG_NET_CLS_U32=y
CONFIG_NET_EMATCH=y
CONFIG_NET_EMATCH_U32=y
CONFIG_NET_SCH_HTB=y
CONFIG_NET_SCH_INGRESS=y

# i cifrari che XFRM pretende, promossi da modulo a builtin per la ragione
#     scritta sopra: compiliamo solo Image.
CONFIG_CRYPTO_CHACHA20POLY1305=y
CONFIG_CRYPTO_NULL=y
CONFIG_CRYPTO_XCBC=y
CONFIG_CRYPTO_CBC=y
CONFIG_CRYPTO_CMAC=y
CONFIG_CRYPTO_CTR=y
CONFIG_CRYPTO_GCM=y
CONFIG_CRYPTO_MD5=y
CONFIG_CRYPTO_SHA1=y
CONFIG_CRYPTO_SHA256=y

# il resto del sottoinsieme di rete.
CONFIG_DUMMY=y

# --- SELinux, che Android non tratta come opzionale.
#
# Senza SELinux nel kernel, servicemanager, hwservicemanager e vndservicemanager
# abortiscono tutti e tre all'avvio: hanno un CHECK sull'apertura dello stato
# SELinux, e se fallisce chiamano abort(). Da li' nessuno diventa context manager
# del binder, ogni processo si blocca su
#   ProcessState: Not able to get context object on /dev/binder
# e il primo servizio con reboot_on_failure — vold — porta giu' il sistema. Il
# sintomo in cima e' "reboot: Restarting system with command 'vold-failed'", che
# non nomina ne' SELinux ne' il binder.
#
# Il segno che manca si vede da ueventd:
#   Cannot get SELinux label on '/dev/dri/renderD128': No data available
# cioe' ENODATA sull'xattr security.selinux, che esiste solo con SELinux attivo.
#
# In Waydroid non si notava perche' il container vedeva il /sys/fs/selinux
# dell'host, e i kernel Ubuntu compilano SELinux anche usando AppArmor.
#
# CONFIG_LSM va riscritto e non solo integrato: abilitare SECURITY_SELINUX non
# basta, il modulo deve comparire nella lista degli LSM attivi, altrimenti resta
# compilato e inerte.
CONFIG_SECURITY=y
CONFIG_SECURITY_NETWORK=y
CONFIG_SECURITY_SELINUX=y
CONFIG_SECURITY_SELINUX_BOOTPARAM=y
CONFIG_SECURITY_SELINUX_DEVELOP=y
CONFIG_LSM="lockdown,yama,loadpin,safesetid,selinux,bpf"

# --- BTF spento: con gcc 15 la generazione fallisce, e qui non serve
# CONFIG_DEBUG_INFO_BTF is not set
CONFIG_DEBUG_INFO_NONE=y
EOF

./scripts/kconfig/merge_config.sh -m .config .config-extra
make ARCH=arm64 olddefconfig

echo "=== verifica delle opzioni che contano"
fail=0
for sym in DRM_VIRTIO_GPU VIRTIO_BLK VIRTIO_NET VIRTIO_INPUT ANDROID_BINDER_IPC \
           ANDROID_BINDERFS DMABUF_HEAPS SW_SYNC OVERLAY_FS EXT4_FS PCI_HOST_GENERIC \
           BRIDGE VETH NF_CONNTRACK NF_NAT IP_NF_NAT IPV6 \
           NETFILTER_XTABLES_LEGACY IP_NF_IPTABLES_LEGACY \
           NETFILTER_XT_TARGET_CHECKSUM IP_NF_MANGLE MEMCG_V1 CPUSETS_V1 \
           SECURITY SECURITY_SELINUX SECURITY_NETWORK PSI \
           IP_ADVANCED_ROUTER IP_MULTIPLE_TABLES \
           NETFILTER_XT_TARGET_MARK NETFILTER_XT_MATCH_MARK \
           NETFILTER_XT_MATCH_OWNER NETFILTER_XT_TARGET_IDLETIMER \
           NETFILTER_XT_MATCH_SOCKET NF_SOCKET_IPV4 NF_SOCKET_IPV6 \
           INET_DIAG_DESTROY INET_UDP_DIAG IPV6_MULTIPLE_TABLES \
           IP6_NF_IPTABLES IP6_NF_FILTER IP6_NF_MANGLE IP_NF_RAW \
           NET_CLS_BPF NET_SCH_INGRESS XFRM_USER \
           SOUND SND SND_PCM SND_VIRTIO; do
    if grep -q "^CONFIG_$sym=y" .config; then
        printf "  %-22s y\n" "$sym"
    else
        printf "  %-22s MANCANTE o modulo: %s\n" "$sym" \
            "$(grep -E "^(CONFIG_$sym=|# CONFIG_$sym is)" .config || echo assente)"
        fail=1
    fi
done
# SECURITY_SELINUX=y non basta: se selinux non e' nella lista degli LSM attivi
# resta compilato e spento, e il guasto che ne segue e' identico a quello di un
# kernel senza SELinux.
lsm_line=$(grep '^CONFIG_LSM=' .config || echo '(assente)')
if ! printf '%s' "$lsm_line" | grep -q selinux; then
    printf "  %-22s selinux assente dalla lista: %s\n" "LSM" "$lsm_line"
    fail=1
else
    printf "  %-22s %s\n" "LSM" "$lsm_line"
fi

if [ "$fail" != 0 ]; then
    echo "FERMO: opzioni indispensabili non abilitate. Il boot fallirebbe o"
    echo "       Android non partirebbe; meglio accorgersene ora che dopo"
    echo "       trenta minuti di compilazione."
    exit 1
fi

echo "=== compilazione del kernel"
make ARCH=arm64 -j"$JOBS" Image

# I moduli si compilano comunque, anche se tutto cio' che serve al boot e'
# builtin. Il motivo e' la lezione del bridge: qualunque simbolo che il
# defconfig lascia a modulo diventa, senza i moduli installati, un fallimento
# a runtime privo di spiegazione. Compilarli costa qualche minuto e chiude la
# categoria invece di riaprirla al prossimo simbolo dimenticato.
echo "=== compilazione dei moduli"
make ARCH=arm64 -j"$JOBS" modules

ls -la arch/arm64/boot/Image
echo "=== moduli compilati: $(find . -name '*.ko*' | wc -l)"
echo "=== fatto: $SRC/arch/arm64/boot/Image"
