#!/bin/bash
# Costruisce una GSI di Android 13 arm64 dall'albero AOSP gia' sincronizzato.
#
# Perche' costruirla invece di scaricarla: per Android 13 Google non pubblica un
# link diretto alla GSI, solo l'Android Flash Tool, che e' interattivo e passa dal
# browser. L'albero AOSP 13 e' gia' sul disco di questa macchina — e' quello che
# ha prodotto hwcomposer.drm.so — quindi la GSI costa solo tempo di CPU e viene
# dalla stessa revisione esatta, android-13.0.0_r84.
#
# Perche' serve. Il system.img di LineageOS per Waydroid ha un SurfaceFlinger
# patchato: attende vendor.waydroid.display@1.0::IWaydroidDisplay, servizio che
# pubblica hwcomposer.waydroid.so, cioe' il compositore che stiamo sostituendo.
# Dipendenza circolare. Una GSI ha un
# SurfaceFlinger normale.
#
# Il vendor NON si tocca: fornisce vulkan.virtio.so, gralloc.minigbm_gbm_mesa e
# libEGL_mesa.so, e dichiara ro.vndk.version=33, che combacia con Android 13.
#
# Tempo: la prima compilazione di un target completo impegna tutti i core per
# un'ora e mezza o due. Il risultato utile e' un solo file, system.img.

set -euo pipefail

TREE="${AOSP_TREE:-$HOME/aosp-13}"
JOBS="${JOBS:-24}"
LUNCH="${LUNCH:-aosp_arm64-userdebug}"

[ -d "$TREE" ] || { echo "FERMO: albero assente in $TREE"; exit 1; }
cd "$TREE"

# Il clang preistorico che serve solo al bitcode di RenderScript e' collegato a
# libncurses.so.5, che le distro moderne non impacchettano piu': Ubuntu 26.04 ha
# soltanto la variante wide, libncursesw.so.6. Senza il collegamento la
# compilazione muore dopo venti minuti di analisi, al primo target di
# RENDERSCRIPT_BITCODE, con quindici errori identici:
#   clang.real: error while loading shared libraries: libncurses.so.5
# Si verifica prima, perche' l'errore arriva tardi e sembra un problema di AOSP.
RS_CLANG="prebuilts/clang/host/linux-x86/clang-3289846/bin/clang.real"
if [ -x "$RS_CLANG" ] && ! "$RS_CLANG" --version >/dev/null 2>&1; then
    cat <<'EOF'
FERMO: il clang di RenderScript non parte, gli manca libncurses.so.5.

Da root, una volta per macchina:

    D=/lib/x86_64-linux-gnu
    ln -sf $D/libncursesw.so.6 $D/libncurses.so.5
    ln -sf $D/libtinfo.so.6    $D/libtinfo.so.5
    ldconfig

Le funzioni che quei binari usano sono identiche fra la 5 e la 6, quindi il
collegamento basta; non esiste un pacchetto libncurses5 su Ubuntu recente.
EOF
    exit 1
fi

echo "=== configurazione: $LUNCH"
# set -u resta spento da qui: envsetup.sh e le funzioni che installa leggono
# variabili non definite e muoiono con "TOP: unbound variable".
set +u
source build/envsetup.sh
lunch "$LUNCH"

echo "=== compilazione della GSI"
m -j"$JOBS"

echo "=== risultato"
OUT="$TREE/out/target/product/generic_arm64"
ls -l "$OUT/system.img"

# Nessuna conversione: contrariamente a quanto si aspetterebbe da un'immagine di
# fabbrica, il system.img del target aosp_arm64 e' **gia' raw**. Provato a passarlo
# a simg2img, che risponde "Failed to read sparse file" — perche' sparso non e'.
#   file system.img -> Linux rev 1.0 ext2 filesystem data (extents) (large files)
# Si monta direttamente con mount -o loop e QEMU lo usa come disco cosi' com'e'.
echo "=== tipo di immagine e impronta"
file "$OUT/system.img"
sha256sum "$OUT/system.img"

echo "=== GSI-PRONTA $OUT/system.img"
