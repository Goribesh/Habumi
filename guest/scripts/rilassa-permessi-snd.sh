#!/bin/bash
# rilassa-permessi-snd.sh -- porta /dev/snd/* da 0660 a 0666 in system.img.
#
# PERCHE'. qemu/probe/sonda-alsa-guest.c deve chiedere al driver virtio-snd se
# concede un anello ALSA piu' grande di 1024 frame (21,3 ms), che e' il tetto di
# tutta la catena audio. Ma /dev/snd/pcmC0D0p e' crw------- system audio, l'utente
# shell non e' nel gruppo audio, e in Waydroid non esistono ne' "adb root" ne'
# "su": la sonda prende Permission denied su sedici prove su sedici. La regola sta
# in /system/etc/ueventd.rc, dentro system.img.
#
# Da eseguire in WSL come root:
#   wsl.exe -u root bash <radice>/guest/scripts/rilassa-permessi-snd.sh
#
# TRE TRAPPOLE, tutte gia' pagate in questo progetto:
#
#  1. Un filesystem pieno fa lasciare a cp un file TRONCATO dentro l'immagine.
#     Qui NON si applica: 0660 e 0666 hanno la stessa lunghezza, quindi il file
#     non cresce di un byte. Lo si verifica comunque prima e dopo.
#  2. Il contesto SELinux va riprodotto: senza, Android rifiuta il file e il
#     sintomo non nomina la causa. Si legge prima e si riapplica dopo.
#  3. Il quoting annidato PowerShell -> wsl -> bash mangia le variabili: per
#     questo esiste questo script invece di una riga di comando.
#
# E' REVERSIBILE: la riserva sta in system.img.riserva-preueventd, verificata per
# MD5 prima di questa modifica.
set -euo pipefail

# La radice del progetto si deriva da dove sta QUESTO file: due livelli sopra
# guest/scripts. Funziona sia in MSYS sia in WSL, perche e il percorso con cui
# lo script e stato invocato -- un percorso assoluto scritto a mano girerebbe
# su una macchina sola.
RADICE="$(cd "$(dirname "$0")/../.." && pwd)"
IMG="$RADICE/guest/images/android/system.img"
MNT=/mnt/sysimg

echo "== 1. controlli preliminari"
[ -f "$IMG" ] || { echo "immagine assente: $IMG"; exit 1; }
ls -l "$IMG"
command -v e2fsck >/dev/null || { echo "e2fsck assente: apt install e2fsprogs"; exit 1; }
command -v getfattr >/dev/null || { echo "getfattr assente: apt install attr"; exit 1; }

echo "== 2. montaggio in lettura/scrittura"
mkdir -p "$MNT"
mountpoint -q "$MNT" && umount "$MNT"
mount -o loop,rw "$IMG" "$MNT"
mount | grep -F "$MNT"

# Il file puo' stare in etc/ (immagine montata su /system) oppure in system/etc/
# (immagine system-as-root). Non si indovina: si cerca.
echo "== 3. trovo ueventd.rc"
RC=""
for c in "$MNT/etc/ueventd.rc" "$MNT/system/etc/ueventd.rc"; do
    if [ -f "$c" ]; then RC="$c"; break; fi
done
if [ -z "$RC" ]; then
    echo "ueventd.rc non trovato. Contenuto della radice dell'immagine:"
    ls "$MNT" | head -20
    umount "$MNT"
    exit 1
fi
echo "trovato: $RC"
PRIMA_BYTE=$(stat -c %s "$RC")
echo "dimensione prima: $PRIMA_BYTE byte"

echo "== 4. la riga da cambiare, come e' adesso"
grep -n "/dev/snd" "$RC" || { echo "nessuna riga /dev/snd"; umount "$MNT"; exit 1; }

echo "== 5. il contesto SELinux, da riprodurre dopo (trappola 2)"
CTX=$(getfattr -n security.selinux --only-values "$RC" 2>/dev/null | tr -d '\0' || true)
echo "contesto: ${CTX:-(nessuno)}"

echo "== 6. la modifica: 0660 -> 0666 sulla sola riga /dev/snd"
# sed sulla SOLA riga che contiene /dev/snd: sostituire 0660 in tutto il file
# cambierebbe i permessi di dispositivi che non c'entrano.
sed -i '\#/dev/snd#s/0660/0666/' "$RC"
grep -n "/dev/snd" "$RC"

DOPO_BYTE=$(stat -c %s "$RC")
echo "dimensione dopo: $DOPO_BYTE byte"
if [ "$PRIMA_BYTE" != "$DOPO_BYTE" ]; then
    echo "ERRORE: la dimensione e' cambiata ($PRIMA_BYTE -> $DOPO_BYTE)."
    echo "Non era previsto: 0660 e 0666 hanno la stessa lunghezza. Mi fermo,"
    echo "e la riserva system.img.riserva-preueventd va ripristinata."
    umount "$MNT"
    exit 1
fi

echo "== 7. riapplico il contesto SELinux"
if [ -n "$CTX" ]; then
    setfattr -n security.selinux -v "$CTX" "$RC"
    echo "riapplicato: $(getfattr -n security.selinux --only-values "$RC" 2>/dev/null | tr -d '\0')"
else
    echo "nessun contesto da riapplicare (l'originale non ne aveva)"
fi

echo "== 8. smonto e verifico la coerenza del filesystem"
sync
umount "$MNT"
# -fy: forza il controllo e risponde si'. Un'immagine incoerente spedita al guest
# da' sintomi che non nominano la causa.
e2fsck -fy "$IMG" || true

echo "== FATTO. Per tornare indietro:"
echo "   cp system.img.riserva-preueventd system.img"
