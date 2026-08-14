#!/bin/bash
# Costruisce l'initramfs che avvia Android senza container.
#
# Va eseguito nella WSL di questa macchina, che e' aarch64: busybox e il
# compositore devono essere per l'architettura del guest, e qui lo sono
# nativamente.
#
# Produce guest/images/android/initramfs.img, che contiene:
#   /init                          lo script che monta e passa a init second_stage
#   /bin/busybox                   statico, unica dipendenza dello script
#   /winq/hwcomposer.drm.so        il compositore DRM compilato su AOSP 13
#   /winq/vendor-props             le righe da aggiungere a vendor/build.prop
#   /winq/HabumiClipboard.apk  l'app degli appunti, installata come app di
#                                  SISTEMA dall'overlay su /system
#   /winq/bootanimation.zip        l'animazione d'avvio, che copre quella in
#                                  /system/product/media
#
# Il resto — system.img, vendor.img, data.img — sta accanto e non viene toccato.
# E' un vincolo di progetto, non una comodita': le immagini devono restare
# scaricabili vergini dall'utente, quindi OGNI personalizzazione passa di qui.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
IMG="$ROOT/guest/images/android"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

if [ "$(uname -m)" != "aarch64" ]; then
    echo "FERMO: host $(uname -m). L'initramfs deve contenere binari aarch64."
    exit 1
fi

BB="$(command -v busybox || true)"
if [ -z "$BB" ]; then
    echo "FERMO: busybox assente. Installalo con:"
    echo "    apt-get install -y busybox-static"
    exit 1
fi
# Uno busybox dinamico dentro un initramfs senza librerie non parte, e l'errore
# che si vede e' un kernel panic che non nomina la causa.
if ! file "$BB" | grep -q 'statically linked'; then
    echo "FERMO: $BB non e' statico. Serve il pacchetto busybox-static."
    exit 1
fi

HWC="$ROOT/guest/images/vendor-overlay/hwcomposer.drm.so"
[ -f "$HWC" ] || { echo "FERMO: manca $HWC"; exit 1; }

# L'APK e l'animazione sono obbligatori come il compositore: se mancassero,
# l'initramfs partirebbe lo stesso e la sola prova sarebbe una riga di kmsg che
# nessuno legge. Meglio non costruirlo affatto.
APPUNTI="$ROOT/guest/appunti-guest/build/HabumiClipboard.apk"
[ -f "$APPUNTI" ] || { echo "FERMO: manca $APPUNTI (costruisci l'app del guest)"; exit 1; }
BOOTANIM="$ROOT/guest/bootanimation/bootanimation.zip"
[ -f "$BOOTANIM" ] || { echo "FERMO: manca $BOOTANIM"; exit 1; }
# asound.conf e' cio' che fa suonare l'audio: senza, l'HAL non trova il
# dispositivo ALSA e il guest resta muto. Stava dentro system.img fino al
# ; ora passa dall'overlay, perche' le immagini si vogliono vergini.
ASOUND="$ROOT/guest/overlay-system/asound.conf"
[ -f "$ASOUND" ] || { echo "FERMO: manca $ASOUND (il guest resterebbe muto)"; exit 1; }

# L'HAL audio con l'anello da 4096 frame. Stava DENTRO vendor.img fino al
#, ed era l'ultima immagine del progetto che non fosse vergine: da
# qui in poi passa dall'overlay, come tutto il resto, e vendor.img si puo'
# scaricare vergine da monte invece di essere redistribuita.
#
# FERMO e non avviso, perche' il sintomo dell'assenza e' muto: senza questo file
# il guest si avvia, suona, e ha l'anello di serie da 1024 frame. Nessun
# messaggio, e cio' che si sente e' lo stutter audio sotto carico che il lavoro
# serviva a togliere. Meglio non costruire l'initramfs affatto.
HALAUDIO="$ROOT/guest/overlay-vendor/audio.primary.waydroid.so"
[ -f "$HALAUDIO" ] || { echo "FERMO: manca $HALAUDIO (l'anello audio tornerebbe a 1024 frame, in silenzio)"; exit 1; }

echo "=== composizione dell'initramfs"
# system_lower e so sono i punti su cui l'init monta l'overlay di sola lettura
# di /system, come vendor_lower e vo lo sono per il vendor.
mkdir -p "$STAGE"/{bin,sbin,proc,sys,dev,tmp,android,winq,vendor_lower,vo,system_lower,so}
cp "$BB" "$STAGE/bin/busybox"
cp "$HERE/android-initramfs-init.sh" "$STAGE/init"
chmod +x "$STAGE/init" "$STAGE/bin/busybox"
cp "$HWC" "$STAGE/winq/hwcomposer.drm.so"
cp "$HALAUDIO" "$STAGE/winq/audio.primary.waydroid.so"
cp "$HERE/android-vendor.props" "$STAGE/winq/vendor-props"
# Le due immagini vogliono l'opposto, e non e' una preferenza: e' un vincolo.
#   - il vendor di Waydroid PRETENDE ro.vndk.lite=true, perche' i suoi HAL legano
#     libhidlbase e libbinder di /system invece dello snapshot VNDK; senza,
#     falliscono con "cannot locate symbol _ZN7android8hardware..."
#   - la GSI la RIFIUTA: linkerconfig esce con
#     "Linkerconfig no longer supports VNDK-Lite configuration", e senza
#     linkerconfig non esiste ld.config.txt, quindi cade tutto cio' che dipende
#     dagli APEX
if [ "${WINQ_VNDK_LITE:-0}" = "1" ]; then
    echo "ro.vndk.lite=true" >> "$STAGE/winq/vendor-props"
    echo "    (ro.vndk.lite=true: per il vendor di Waydroid con la sua immagine)"
fi
cp "$APPUNTI" "$STAGE/winq/HabumiClipboard.apk"
cp "$BOOTANIM" "$STAGE/winq/bootanimation.zip"
cp "$ASOUND" "$STAGE/winq/asound.conf"
cp "$HERE/android-fstab" "$STAGE/winq/fstab.waydroid"
cp "$HERE/android-tablet.idc" "$STAGE/winq/QEMU_Virtio_Tablet.idc"
# La diagnostica si include solo se WINQ_LOGCAT=1: allaga la seriale, e serve
# mentre si inseguono i guasti dell'avvio, non dopo.
if [ "${WINQ_LOGCAT:-0}" = "1" ]; then
    cp "$HERE/android-winq-logcat.rc" "$STAGE/winq/winq-logcat.rc"
    cp "$HERE/android-winq-logcat.sh" "$STAGE/winq/winq-logcat.sh"
    echo "    (incluso il logcat in diretta sulla seriale)"
fi
# La sonda interna: risponde a domande che il log di init non chiude, tipo quale
# modulo hwcomposer sia davvero in memoria. Anch'essa opzionale.
if [ "${WINQ_PROBE:-0}" = "1" ]; then
    cp "$HERE/android-winq-probe.rc" "$STAGE/winq/winq-probe.rc"
    cp "$HERE/android-winq-probe.sh" "$STAGE/winq/winq-probe.sh"
    echo "    (inclusa la sonda interna)"
fi

mkdir -p "$IMG"
( cd "$STAGE" && find . -print0 | cpio --null -o -H newc --quiet ) | gzip -9 > "$IMG/initramfs.img"

echo "=== risultato"
ls -l "$IMG/initramfs.img"
echo "contenuto:"
# Il "./" e' opzionale nel filtro perche' i cpio recenti (Ubuntu 26.04) normalizzano
# i nomi e li elencano senza prefisso, mentre quelli vecchi li lasciano come li ha
# scritti find. Col filtro ancorato a "^\./" il grep non trovava piu' nulla, e sotto
# `set -o pipefail` quel grep a vuoto faceva uscire lo script con 1 DOPO aver scritto
# un'immagine perfettamente valida: un finto fallimento che invita a rifare la build
# o a cercare un guasto che non c'e'. Il `|| true` serve allo stesso scopo: questa e'
# una stampa di controllo, non un cancello.
zcat "$IMG/initramfs.img" | cpio -t --quiet 2>/dev/null | grep -E '^(\./)?(init|bin/busybox|winq/)' | sed 's/^/  /' || true
