#!/bin/bash
# installa-hal-4096.sh -- mette in vendor.img l'HAL audio con anello da 4096 frame.
#
# COSA E' CAMBIATO NELL'HAL. Due byte, verificati con cmp -l: il campo imm16 di
# due istruzioni MOVZ passa da 0x1000 (4096 byte = 1024 frame) a 0x4000
# (16384 byte = 4096 frame). Uno dei due siti dichiara la dimensione ad
# AudioFlinger, l'altro apre il PCM: servono entrambi, altrimenti le due viste
# divergono. Dettaglio del ritrovamento e dell'encoding nel commento di
# patcha-hal.py, nella cartella temporanea della sessione.
#
# PERCHE' 4096 FRAME. qemu/probe/sonda-alsa-guest.c ha verificato che il driver
# virtio-snd CONCEDE fino a 8192 frame (170,7 ms), otto prove su otto sul
# percorso diretto hw:0,0. 4096/8 da' 85,3 ms di anello con periodi da 10,67 ms:
# quattro volte la riserva attuale, con un periodo che resta sano.
#
# PERCHE' SERVE. L'anello da 1024 frame (21,3 ms) e' il TETTO di tutta la catena
# audio: limita il buffer host (che non puo' riempirsi oltre cio' che il guest
# consegna) e ogni riserva a monte. Con throughput al 100% e ogni elemento
# dimensionato al pareggio, qualunque jitter diventa udibile -- 12 silenzi da 5 a
# 29 ms in 30 s, che l'utente sente in cuffia.
#
# E' UN ESPERIMENTO. Patchare un .so spogliato poggia su semantica dedotta
# dall'uso dei registri. L'esito e' pero' verificabile senza ambiguita':
#   /proc/asound/card0/pcm0p/sub0/hw_params   -> buffer_size deve dire 4096
#   dumpsys media.audio_flinger               -> HAL frame count deve concordare
# Se l'audio non parte, si ripristina:
#   cp guest/images/android/vendor.img.riserva-prehal \
#      guest/images/android/vendor.img
#
# Da eseguire in WSL come root:
#   wsl.exe -u root bash /mnt/c/.../guest/scripts/installa-hal-4096.sh
set -euo pipefail

# La radice del progetto si deriva da dove sta QUESTO file: due livelli sopra
# guest/scripts. Funziona sia in MSYS sia in WSL, perche e il percorso con cui
# lo script e stato invocato -- un percorso assoluto scritto a mano girerebbe
# su una macchina sola.
RADICE="$(cd "$(dirname "$0")/../.." && pwd)"
IMG="$RADICE/guest/images/android/vendor.img"
# Il .so patchato: si passa in NUOVO, oppure si mette accanto alle immagini.
# Prima ci stava un percorso della cartella temporanea di una sessione di
# lavoro: un valore usa-e-getta buono per nessuno.
NUOVO="${NUOVO:-$RADICE/guest/images/hal-4096.so}"
MNT=/mnt/vendorimg
REL=lib64/hw/audio.primary.waydroid.so

echo "== 1. controlli"
[ -f "$IMG" ] || { echo "immagine assente: $IMG"; exit 1; }
[ -f "$NUOVO" ] || { echo "HAL patchato assente: $NUOVO"; exit 1; }
echo "HAL nuovo: $(stat -c %s "$NUOVO") byte"

echo "== 2. montaggio"
mkdir -p "$MNT"
mountpoint -q "$MNT" && umount "$MNT"
mount -o loop,rw "$IMG" "$MNT"
df -h "$MNT" | tail -1

VEC="$MNT/$REL"
[ -f "$VEC" ] || { echo "manca $VEC"; ls "$MNT" | head; umount "$MNT"; exit 1; }

echo "== 3. il file attuale, e i suoi attributi da riprodurre"
ls -l "$VEC"
VECCHIO_BYTE=$(stat -c %s "$VEC")
MODO=$(stat -c %a "$VEC")
PROP=$(stat -c %u:%g "$VEC")
CTX=$(getfattr -n security.selinux --only-values "$VEC" 2>/dev/null | tr -d '\0' || true)
echo "dimensione=$VECCHIO_BYTE modo=$MODO proprietario=$PROP contesto=${CTX:-(nessuno)}"

NUOVO_BYTE=$(stat -c %s "$NUOVO")
echo "dimensione nuova: $NUOVO_BYTE byte"
# La prima versione di questo script pretendeva dimensioni IDENTICHE, perche'
# nasceva per una patch di due byte in place. Ora il .so e' ricostruito da
# sorgente, quindi la dimensione cambia legittimamente. Cio' che conta e' che ci
# stia: un cp che esaurisce il filesystem lascia un file TRONCATO dentro
# l'immagine, ed e' la prima delle tre trappole documentate.
if [ "$NUOVO_BYTE" -gt "$VECCHIO_BYTE" ]; then
    CRESCITA=$((NUOVO_BYTE - VECCHIO_BYTE))
    LIBERO=$(df --output=avail -B1 "$MNT" | tail -1)
    echo "cresce di $CRESCITA byte; liberi nel filesystem: $LIBERO"
    # Margine di sicurezza: si pretende il doppio della crescita.
    if [ "$LIBERO" -lt $((CRESCITA * 2)) ]; then
        echo "ERRORE: spazio insufficiente con margine. Mi fermo invece di"
        echo "rischiare un file troncato dentro l'immagine."
        umount "$MNT"
        exit 1
    fi
fi

echo "== 4. riserva del .so ORIGINALE dentro l'immagine stessa"
# Costa 19880 byte e permette di tornare indietro senza rimontare l'immagine
# grande. Il filesystem ha spazio: verificato al passo 2.
cp -a "$VEC" "$VEC.originale-1024" 2>/dev/null || {
    echo "impossibile creare la riserva interna: si procede comunque, la"
    echo "riserva dell'immagine intera esiste (vendor.img.riserva-prehal)"
}

echo "== 5. scrittura"
# cat > invece di cp: mantiene inode, modo, proprietario e contesto SELinux del
# file esistente, quindi non c'e' niente da riapplicare e la trappola del
# contesto perso non si presenta.
cat "$NUOVO" > "$VEC"
sync

echo "== 6. verifica"
ls -l "$VEC"
echo "dimensione dopo: $(stat -c %s "$VEC") byte"
echo "contesto dopo:   $(getfattr -n security.selinux --only-values "$VEC" 2>/dev/null | tr -d '\0')"
echo "md5 nell'immagine: $(md5sum "$VEC" | cut -d' ' -f1)"
echo "md5 del file nuovo: $(md5sum "$NUOVO" | cut -d' ' -f1)"

echo "== 7. smonto e controllo la coerenza"
umount "$MNT"
e2fsck -fy "$IMG" || true

echo "== FATTO."
echo "Verifica dopo l'avvio:"
echo "  adb shell cat /proc/asound/card0/pcm0p/sub0/hw_params   -> buffer_size 4096"
echo "Per tornare indietro:"
echo "  cp guest/images/android/vendor.img.riserva-prehal guest/images/android/vendor.img"
