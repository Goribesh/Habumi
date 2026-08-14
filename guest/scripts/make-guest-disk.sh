#!/bin/bash
# Costruisce il disco del guest: un'immagine ext4 avviabile con la userspace
# che serve a far girare Android.
#
# Va eseguito dentro la distro WSL, come root, perche' copia il suo stesso
# rootfs. Quella distro ha gia' tutto: systemd, waydroid 1.6.2, lxc 6.0.6,
# weston e le immagini Android in /var/lib/waydroid/images. Ricostruirla da
# zero in una VM significherebbe rifare un lavoro che nella sessione
# precedente e' stato fatto e verificato.
#
# L'immagine si costruisce **dentro** il filesystem della WSL e si copia su
# Windows solo alla fine. Costruirla direttamente su /mnt/c sarebbe molto piu'
# lento: la copia del rootfs sono centinaia di migliaia di file piccoli, e
# ognuno paga l'attraversamento di 9p. Una singola copia sequenziale alla fine
# costa una frazione.

set -euo pipefail

SIZE="${SIZE:-24G}"
# Eseguito come root, $HOME e' /root, ma i sorgenti del kernel e l'immagine
# stanno nella home dell'utente reale: la si risolve esplicitamente, cosi' lo
# script funziona sia via `wsl -u root` sia via sudo.
#
# Scritto in forma piatta di proposito. La versione compatta
#   SRCHOME="${SRCHOME:-$(getent passwd "${SUDO_USER:-x}" | cut -d: -f6)}"
# annida ${...} dentro $(...) dentro ${...}, e bash 5.3 la parsa male: chiude
# l'espansione sulla prima graffa che trova piu' avanti nel file, e l'errore
# risultante indica una riga che non ha nulla a che vedere col problema.
REALUSER="${SUDO_USER:-$(id -un)}"
if [ -z "${SRCHOME:-}" ]; then
    SRCHOME=$(getent passwd "$REALUSER" | cut -d: -f6)
fi
[ -d "$SRCHOME" ] || SRCHOME="$HOME"
IMG="${IMG:-$SRCHOME/guest-root.img}"
MNT="${MNT:-/mnt/guestroot}"
LABEL="guestroot"
DEST="${DEST:-$(cd "$(dirname "$0")/../.." && pwd)/guest/images/guest-root.img}"

[ "$(id -u)" = 0 ] || { echo "serve root"; exit 1; }

echo "=== immagine $IMG da $SIZE"
rm -f "$IMG"
fallocate -l "$SIZE" "$IMG"
mkfs.ext4 -q -F -L "$LABEL" "$IMG"

mkdir -p "$MNT"
mountpoint -q "$MNT" && umount "$MNT"
mount -o loop "$IMG" "$MNT"
trap 'umount "$MNT" 2>/dev/null || true' EXIT

echo "=== copia del rootfs (esclusi i filesystem virtuali)"
# --numeric-ids: non si passa per il database utenti dell'host.
# -H -A -X: hard link, ACL, xattr — Android e lxc ne fanno uso.
rsync -aHAX --numeric-ids --info=progress2 \
    --exclude='/dev/*' --exclude='/proc/*' --exclude='/sys/*' \
    --exclude='/run/*' --exclude='/tmp/*' --exclude='/mnt/*' \
    --exclude='/media/*' --exclude='/lost+found' \
    --exclude="$IMG" --exclude='/swapfile' \
    --exclude="$SRCHOME/linux-*" --exclude="$SRCHOME/*.tar.xz" \
    / "$MNT/"

echo
echo "=== adattamenti per il boot in QEMU"

# La WSL non usa fstab: il guest sì.
cat > "$MNT/etc/fstab" <<EOF
LABEL=$LABEL  /      ext4  defaults,noatime  0 1
tmpfs         /tmp   tmpfs defaults,nosuid   0 0
EOF

# Login automatico sulla console seriale. Serve perche' il guest viene
# pilotato da guest_console.py, che legge e scrive su quella console: una
# richiesta di password la bloccherebbe al primo passo.
mkdir -p "$MNT/etc/systemd/system/serial-getty@ttyAMA0.service.d"
cat > "$MNT/etc/systemd/system/serial-getty@ttyAMA0.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I 115200 linux
EOF

# Rete: DHCP su qualunque interfaccia virtio. Senza questo il guest parte ma
# non raggiunge la rete, e apk/apt non funzionano.
mkdir -p "$MNT/etc/systemd/network"
cat > "$MNT/etc/systemd/network/20-virtio.network" <<'EOF'
[Match]
Name=en* eth*

[Network]
DHCP=yes
EOF
ln -sf /lib/systemd/system/systemd-networkd.service \
    "$MNT/etc/systemd/system/multi-user.target.wants/systemd-networkd.service" 2>/dev/null || true
ln -sf /lib/systemd/system/systemd-resolved.service \
    "$MNT/etc/systemd/system/multi-user.target.wants/systemd-resolved.service" 2>/dev/null || true

# Roba specifica della WSL che nel guest non ha senso. /etc/resolv.conf nella
# WSL e' un file generato con il DNS dell'host Windows: lasciarlo puntare la'
# renderebbe la risoluzione dei nomi muta.
rm -f "$MNT/etc/wsl.conf" "$MNT/init"
rm -f "$MNT/etc/resolv.conf"
ln -sf ../run/systemd/resolve/stub-resolv.conf "$MNT/etc/resolv.conf"

# Il guest non ha /dev popolato da WSL: devtmpfs lo fa, ma le directory di
# mount devono esistere.
mkdir -p "$MNT/dev" "$MNT/proc" "$MNT/sys" "$MNT/run" "$MNT/tmp" "$MNT/mnt"
chmod 1777 "$MNT/tmp"

sync
df -h "$MNT" | tail -1
umount "$MNT"
trap - EXIT

echo "=== copia su Windows: $DEST"
mkdir -p "$(dirname "$DEST")"
# La copia e' sequenziale e grande: si mostra il progresso perche' dura minuti.
dd if="$IMG" of="$DEST" bs=16M status=progress conv=fsync

ls -la "$DEST"
echo "=== fatto"
