#!/bin/bash
# aggiungi-chmod-snd.sh -- fa applicare a init il permesso su /dev/snd.
#
# PERCHE' NON BASTA ueventd.rc. La riga /dev/snd/* e' stata portata a 0666 in
# /system/etc/ueventd.rc (script rilassa-permessi-snd.sh) e il file nell'immagine
# lo conferma, ma i nodi restano crw------- (0600). E non e' che ueventd sia
# rotto: applica correttamente il modo a tutto il resto, verificato sullo stesso
# avvio --
#
#     /dev/input/event0    crw-rw----  root input       (regola 0660)  OK
#     /dev/graphics/fb0    crw-rw----  root graphics    (regola 0660)  OK
#     /dev/tun             crw-rw----  system vpn       (regola 0660)  OK
#     /dev/snd/pcmC0D0p    crw-------  system audio     (regola 0666)  NO
#
# Su /dev/snd applica il PROPRIETARIO (system audio, che viene dalla regola) ma
# NON il modo. La causa non e' stata trovata: il core ALSA crea i nodi a 0600
# (il suo devnode non imposta *mode, quindi vale il default di devtmpfs) e
# nessun parametro del modulo snd espone un device_mode -- verificato, in
# /sys/module/snd/parameters ci sono solo cards_limit, major,
# max_user_ctl_alloc_size, slots. Resta un'anomalia aperta.
#
# Invece di combatterla si usa init, che gira come root e il cui chmod e'
# incondizionato. Costa due righe e non dipende da perche' ueventd si comporti
# cosi'.
#
# A COSA SERVE. qemu/probe/sonda-alsa-guest.c deve chiedere al driver
# virtio-snd se concede un anello ALSA piu' grande di 1024 frame (21,3 ms), che
# e' il tetto di tutta la catena audio e quindi la sola leva che aumenti la
# riserva. Senza accesso a /dev/snd la sonda prende Permission denied.
#
# QUESTO INDEBOLISCE L'IMMAGINE, e va detto: /dev/snd diventa apribile da
# qualunque processo. E' uno stato di DIAGNOSI, non da spedire. Si torna indietro
# con la riserva:
#     cp guest/images/android/system.img.riserva-preueventd \
#        guest/images/android/system.img
#
# Da eseguire in WSL come root:
#   wsl.exe -u root bash /mnt/c/.../guest/scripts/aggiungi-chmod-snd.sh
set -euo pipefail

# La radice del progetto si deriva da dove sta QUESTO file: due livelli sopra
# guest/scripts. Funziona sia in MSYS sia in WSL, perche e il percorso con cui
# lo script e stato invocato -- un percorso assoluto scritto a mano girerebbe
# su una macchina sola.
RADICE="$(cd "$(dirname "$0")/../.." && pwd)"
IMG="$RADICE/guest/images/android/system.img"
MNT=/mnt/sysimg
RC_REL=system/etc/init/winq-snd-diag.rc

echo "== 1. montaggio"
mkdir -p "$MNT"
mountpoint -q "$MNT" && umount "$MNT"
mount -o loop,rw "$IMG" "$MNT"

DIR="$MNT/system/etc/init"
[ -d "$DIR" ] || { echo "manca $DIR"; umount "$MNT"; exit 1; }

echo "== 2. spazio libero (il filesystem e' al 98,7%: un cp che lo esaurisce"
echo "      lascerebbe un file TRONCATO dentro l'immagine)"
df -h "$MNT" | tail -1

echo "== 3. il contesto SELinux da imitare, letto da un file gemello"
GEMELLO=$(ls "$DIR"/*.rc | head -1)
CTX=$(getfattr -n security.selinux --only-values "$GEMELLO" 2>/dev/null | tr -d '\0' || true)
echo "gemello: $(basename "$GEMELLO")   contesto: ${CTX:-(nessuno)}"
if [ -z "$CTX" ]; then
    echo "nessun contesto sul gemello: mi fermo invece di scrivere un file che"
    echo "Android potrebbe rifiutare senza nominare la causa."
    umount "$MNT"
    exit 1
fi

echo "== 4. scrivo la regola"
# on property invece di "on boot": i nodi di /dev/snd esistono gia' a boot, ma
# aspettare boot_completed rende l'azione indipendente dall'ordine di creazione
# dei dispositivi, che e' proprio la cosa che non abbiamo capito con ueventd.
cat > "$MNT/$RC_REL" <<'EOF'
# winq: apre /dev/snd alla sonda di diagnosi. STATO DI DIAGNOSI, non da
# spedire: vedi guest/scripts/aggiungi-chmod-snd.sh per il perche' e per come
# tornare indietro.
on property:sys.boot_completed=1
    chmod 0666 /dev/snd/controlC0
    chmod 0666 /dev/snd/pcmC0D0p
    chmod 0666 /dev/snd/pcmC0D0c
    chmod 0666 /dev/snd/timer
EOF
chmod 644 "$MNT/$RC_REL"
chown 0:0 "$MNT/$RC_REL"
setfattr -n security.selinux -v "$CTX" "$MNT/$RC_REL"

echo "== 5. verifica di cio' che e' stato scritto"
ls -l "$MNT/$RC_REL"
echo "contesto: $(getfattr -n security.selinux --only-values "$MNT/$RC_REL" 2>/dev/null | tr -d '\0')"
echo "--- contenuto ---"
cat "$MNT/$RC_REL"

echo "== 6. smonto e controllo la coerenza"
sync
umount "$MNT"
e2fsck -fy "$IMG" || true

echo "== FATTO. Per tornare indietro:"
echo "   cp system.img.riserva-preueventd system.img"
