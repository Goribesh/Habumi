#!/bin/bash
# Costruisce hwcomposer.drm.so per Android 13 arm64.
#
# **Va eseguito su una macchina Linux x86_64**, non su questo PC. Il manifest di
# android-13 ha solo prebuilts/clang/host/darwin-x86 e linux-x86, e zero
# riferimenti a host arm64: AOSP 13 si costruisce solo su host x86_64, e la WSL
# di questa macchina e' arm64, quindi non puo' eseguire quei binari.
#
# Cosa produce, e perche' serve. L'immagine Android che usiamo ha il percorso di
# rendering giusto — vulkan.virtio.so (Venus), libEGL_mesa.so, gralloc.gbm.so —
# ma il suo unico compositor hardware e' hwcomposer.waydroid.so, che consegna i
# frame a un server Wayland. Per l'applicazione Windows serve invece che Android
# faccia scanout su /dev/dri/card0: il kernel guest emette SET_SCANOUT via
# virtio-gpu, il ponte riceve la texture e la finestra la presenta.
#
# Il vendor dichiara android.hardware.graphics.composer@2.1::IComposer in
# passthrough, quindi il servizio generico carica
# hwcomposer.<ro.hardware.hwcomposer>.so: sostituire il compositor e' previsto
# dal meccanismo, basta aggiungere il .so e cambiare quella proprieta'.
#
# Spazio e tempo, misurati su un albero android-13 con --depth=1:
#   ~100 GB di disco, sincronizzazione di ore su gigabit, poi la prima
#   compilazione impegna tutti i core per un'oretta. Con 32 core e 32 GB di RAM
#   conviene tenere -j24: soong e' avido di memoria e a -j32 rischia l'OOM.

set -euo pipefail

TAG="${AOSP_TAG:-android-13.0.0_r84}"
TREE="${AOSP_TREE:-$HOME/aosp-13}"
JOBS="${JOBS:-24}"
# Separato da JOBS di proposito: vedi il commento sul 429 piu' sotto.
SYNC_JOBS="${SYNC_JOBS:-8}"
# Il target di Cuttlefish e' un device completo, quindi la configurazione
# include il vendor. Un target GSI come aosp_arm64 costruisce il solo system e
# puo' escludere i moduli vendor.
LUNCH="${LUNCH:-aosp_cf_arm64_only_phone-userdebug}"

if [ "$(uname -m)" != "x86_64" ]; then
    echo "FERMO: host $(uname -m). AOSP 13 richiede un host x86_64."
    exit 1
fi

# --- WSL2: due trappole che costano ore ------------------------------------
if grep -qi microsoft /proc/version 2>/dev/null; then
    echo "=== rilevato WSL2"

    # 1. L'albero non puo' stare su /mnt/c. Il filesystem di Windows non e'
    #    case sensitive, e AOSP contiene file che differiscono solo per il
    #    maiuscolo: la sincronizzazione sembra riuscire e la compilazione
    #    fallisce piu' tardi con errori incomprensibili. E ogni accesso
    #    attraversa 9p, che su milioni di file piccoli e' lentissimo.
    case "$TREE" in
        /mnt/*)
            echo "FERMO: \$AOSP_TREE e' su $TREE, cioe' un disco Windows."
            echo "       AOSP va costruito nel filesystem della WSL, per esempio"
            echo "       ~/aosp-13. Il filesystem di Windows non e' case sensitive"
            echo "       e AOSP contiene file che differiscono solo per il maiuscolo."
            exit 1
            ;;
    esac

    # 2. WSL2 per default concede alla VM metà della RAM. Su 32 GB sono 16, e
    #    soong con -j24 ne chiede di piu': l'OOM killer arriva a build avanzata.
    mem_gb=$(awk '/MemTotal/{printf "%d", $2/1048576}' /proc/meminfo)
    echo "    RAM disponibile alla WSL: ${mem_gb} GB"
    if [ "$mem_gb" -lt 20 ]; then
        cat <<'EOF'
    ATTENZIONE: meno di 20 GB visibili. Conviene alzare il limite creando
    C:\Users\<utente>\.wslconfig con:

        [wsl2]
        memory=24GB
        swap=16GB
        processors=24

    poi `wsl --shutdown` e riapri la shell. Senza questo, soong con -j24 puo'
    finire ucciso dall'OOM killer dopo mezz'ora di compilazione.
EOF
    fi
fi

# Lo spazio va verificato prima, non a sincronizzazione avviata: l'albero piu'
# la directory out arrivano intorno ai 250 GB, e in WSL2 crescono dentro il
# vhdx, quindi consumano spazio sul disco Windows.
avail_gb=$(df -BG --output=avail "$(dirname "$TREE")" 2>/dev/null | tail -1 | tr -dc '0-9')
echo "=== spazio disponibile: ${avail_gb:-?} GB (servono ~250)"
if [ -n "$avail_gb" ] && [ "$avail_gb" -lt 250 ]; then
    echo "FERMO: spazio insufficiente. L'albero con --depth=1 sono ~100 GB,"
    echo "       la directory out un'altra centinaia e mezza."
    exit 1
fi

echo "=== prerequisiti"
missing=""
for b in git python3 repo curl unzip zip rsync bison flex bc; do
    command -v "$b" >/dev/null || missing="$missing $b"
done
if [ -n "$missing" ]; then
    cat <<EOF
FERMO: mancano:$missing

Su Debian/Ubuntu:
    sudo apt install -y git-core gnupg flex bison build-essential zip curl \\
        zlib1g-dev libc6-dev-i386 x11proto-core-dev libx11-dev lib32z1-dev \\
        libgl1-mesa-dev libxml2-utils xsltproc unzip fontconfig rsync bc \\
        python3 openjdk-17-jdk

E per repo:
    mkdir -p ~/bin && curl -o ~/bin/repo https://storage.googleapis.com/git-repo-downloads/repo
    chmod +x ~/bin/repo && export PATH=~/bin:\$PATH
EOF
    exit 1
fi

echo "=== albero $TAG in $TREE"
mkdir -p "$TREE"
cd "$TREE"
if [ ! -d .repo ]; then
    # --depth=1 e --no-tags: serve il contenuto, non la storia. Fa la
    # differenza fra ~100 GB e diverse centinaia.
    #
    # Niente --partial-clone con --clone-filter=blob:limit=10M, provato e
    # scartato: quel filtro rimanda al checkout i blob oltre i 10 MB, e il
    # checkout li richiede al server uno per uno, in serie. Sui quattro
    # repository con i blob piu' grossi — external/tensorflow,
    # prebuilts/clang/host/linux-x86, prebuilts/module_sdk/art,
    # external/fonttools — la sincronizzazione moriva ogni volta con
    #   platform/external/tensorflow checkout c8c3848d...
    # Risparmiare banda nel fetch per poi perdere il checkout non e' un
    # affare: la banda qui non e' il collo di bottiglia, il limite di
    # richieste del server lo e'.
    repo init -u https://android.googlesource.com/platform/manifest \
        -b "$TAG" --depth=1 --no-tags
fi

# Riparazione di un albero inizializzato in precedenza col filtro sui blob. Il
# filtro NON sta nei singoli project-objects: repo lo tiene in manifests.git e
# lo passa ai fetch dei progetti, quindi e' li' che va togliexto. (Cercarlo nei
# project-objects e' inutile: verificato, zero occorrenze.)
if [ -d .repo/manifests.git ]; then
    for k in repo.partialclone repo.clonefilter remote.origin.partialclonefilter; do
        git --git-dir=.repo/manifests.git config --unset-all "$k" 2>/dev/null || true
    done
    echo "=== filtro sui blob disattivato per i fetch successivi"
fi
# Niente --fail-fast: la sincronizzazione tira giu' migliaia di repository per
# ore, e un singolo fetch che inciampa e' normale. Con --fail-fast quel singolo
# inciampo butta via tutto il lavoro fatto fino a quel punto — ed e' esattamente
# come e' morto il primo tentativo, senza nemmeno lasciare l'errore nel log.
# --force-sync ripara gli alberi rimasti a metà dal tentativo precedente.
# Il parallelismo della sincronizzazione non e' quello della compilazione, e
# confonderli costa ore. Con -j24 i server git di Google rispondono
#   error: RPC failed; HTTP 429
# cioe' limitano le richieste, e il sync fallisce a raffica lasciando alberi di
# lavoro a metà ("Cannot initialize work tree"). Otto e' la soglia pratica.
# --force-sync ripara quelli rimasti rotti dai tentativi precedenti.
#
# Niente --optimized-fetch, ed e' la lezione piu' costosa di questa build.
# Quell'opzione salta il fetch di un progetto la cui revisione sembra gia'
# presente. Dopo che il 429 aveva troncato quattro fetch — external/tensorflow,
# external/fonttools, prebuilts/clang/host/linux-x86, prebuilts/module_sdk/art —
# il ref c'era ma gli oggetti no: ogni tentativo successivo li scavalcava e
# moriva sul checkout in trenta secondi, senza scaricare nulla. Il ciclo di
# ritentativi girava a vuoto perche' l'ottimizzazione gli impediva di riparare.
tries=0
until repo sync -c -j"$SYNC_JOBS" --no-tags --force-sync; do
    tries=$((tries + 1))
    if [ "$tries" -ge 8 ]; then
        echo "FERMO: repo sync fallito $tries volte di seguito."
        exit 1
    fi
    echo "=== sync interrotto, tentativo $((tries + 1)) di 8 fra 30 secondi"
    sleep 30
done

echo "=== configurazione: $LUNCH"
# set -u resta spento da qui alla fine, e non va rimesso. envsetup.sh e le
# funzioni che installa — lunch, m, gettop — leggono variabili non definite, e
# con set -u attivo la prima lettura le uccide:
#   build/envsetup.sh: line 935: TOP: unbound variable
#   Couldn't locate the top of the tree. Try setting TOP.
# Il messaggio inganna: TOP non serve impostarlo, e' set -u il problema. Le
# funzioni ne leggono diverse altre, quindi esportare TOP a mano sposterebbe
# solo l'errore alla successiva.
set +u
source build/envsetup.sh
lunch "$LUNCH"

# Solo il modulo che ci serve. Un `m` senza argomenti costruirebbe l'immagine
# intera, che qui non interessa: il prodotto e' un singolo .so da innestare in
# un vendor esistente.
echo "=== compilazione di hwcomposer.drm"
m -j"$JOBS" hwcomposer.drm

echo "=== risultato"
find out/target/product -name 'hwcomposer.drm.so' -printf '%p  %s byte\n'

cat <<'EOF'

=== cosa portare indietro

Il file hwcomposer.drm.so trovato qui sopra. Va copiato nel vendor
dell'immagine Android, in /vendor/lib64/hw/, e va impostata la proprieta'

    ro.hardware.hwcomposer=drm

Il vendor dichiara il composer HIDL 2.1 in passthrough, quindi il servizio
generico caricera' hwcomposer.drm.so al posto di hwcomposer.waydroid.so.

Verificare anche le dipendenze dinamiche del .so contro quelle presenti nel
vendor:

    readelf -d hwcomposer.drm.so | grep NEEDED

Se nomina librerie assenti nell'immagine, servono anche quelle — ed e' il
motivo per cui conviene costruire con il target di un device completo invece
che con una GSI.
EOF
