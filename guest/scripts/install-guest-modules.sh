#!/bin/bash
# Installa i moduli del kernel nel disco del guest e lo ricopia su Windows.
#
# Va eseguito nella distro WSL come root, dopo build-guest-kernel.sh.
#
# Serve perche' i simboli che il defconfig lascia a modulo, senza i moduli
# installati, diventano fallimenti a runtime privi di spiegazione. Il caso che
# ha portato a questo script: CONFIG_BRIDGE restava a modulo, il modulo non
# c'era, e Waydroid moriva su "ip link add type bridge: Unknown device type"
# dopo che tutto il resto — weston su DRM compreso — funzionava.
#
# Applica anche le proprieta' Android definitive, cosi' non vanno riscritte a
# ogni sessione: nella distro WSL erano gralloc=default ed egl=swiftshader,
# cioe' rendering su CPU, perche' la' non c'era un render node.

set -euo pipefail

REALUSER="${SUDO_USER:-$(id -un)}"
SRCHOME=$(getent passwd "$REALUSER" | cut -d: -f6)
[ -d "$SRCHOME" ] || SRCHOME="$HOME"

KVER="${KERNEL_VERSION:-6.18.35}"
SRC="$SRCHOME/linux-$KVER"
IMG="${IMG:-$SRCHOME/guest-root.img}"
MNT="${MNT:-/mnt/guestroot}"
DEST="${DEST:-$(cd "$(dirname "$0")/../.." && pwd)/guest/images/guest-root.img}"

[ "$(id -u)" = 0 ] || { echo "serve root"; exit 1; }
[ -d "$SRC" ] || { echo "sorgenti del kernel assenti: $SRC"; exit 1; }
[ -f "$IMG" ] || { echo "immagine assente: $IMG"; exit 1; }

mkdir -p "$MNT"
mountpoint -q "$MNT" && umount "$MNT"
mount -o loop "$IMG" "$MNT"
trap 'umount "$MNT" 2>/dev/null || true' EXIT

echo "=== installazione dei moduli in $MNT"
make -C "$SRC" ARCH=arm64 INSTALL_MOD_PATH="$MNT" modules_install > /dev/null
KREL=$(cat "$SRC/include/config/kernel.release")
echo "    versione: $KREL"
echo "    moduli:   $(find "$MNT/lib/modules/$KREL" -name '*.ko*' | wc -l)"

echo "=== proprieta' Android per il percorso hardware"
PROP="$MNT/var/lib/waydroid/waydroid_base.prop"
if [ -f "$PROP" ]; then
    # Su una riga sola: la continuazione con backslash dentro le virgolette
    # singole non viene interpretata dalla shell, finisce dentro l'espressione
    # e sed risponde "unterminated address regex".
    sed -i -e 's/^ro.hardware.gralloc=.*/ro.hardware.gralloc=gbm/' -e 's/^ro.hardware.egl=.*/ro.hardware.egl=mesa/' "$PROP"
    grep -E 'gralloc|egl' "$PROP" | sed 's/^/    /'
else
    echo "    ATTENZIONE: $PROP assente"
fi

sync
umount "$MNT"
trap - EXIT

echo "=== copia su Windows"
dd if="$IMG" of="$DEST" bs=16M status=none conv=fsync
ls -la "$DEST"
echo "=== fatto"
