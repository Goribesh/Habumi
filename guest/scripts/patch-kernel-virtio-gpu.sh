#!/bin/sh
# Insegna a virtio-gpu i formati che Android usa per lo scanout.
#
# PERCHE' ESISTE QUESTO SCRIPT, e non solo il documento in guest/patches/.
# La modifica fu applicata a mano all'albero del kernel durante la fase 1, e
# guest/patches/kernel-virtio-gpu-formati-android.patch e' la sua DOCUMENTAZIONE,
# non una patch applicabile ("patch: Only garbage was found in the patch input").
# Risultato: il kernel che funziona esisteva come file binario e nessuno poteva
# ricostruirlo. Scoperto ricompilando il kernel per aggiungere il
# routing per criteri: due build di fila avviavano Android senza mostrare nulla,
# con la finestra su "Display output is not active", e la causa era proprio questa
# modifica assente.
#
# IL DIFETTO CHE RISOLVE. Il driver dichiara un solo formato per il piano primario,
# DRM_FORMAT_HOST_XRGB8888, e rifiuta tutto il resto in
# virtio_gpu_user_framebuffer_create con ENOENT. Android compone in
# RGBX_8888 / RGBA_8888, che in DRM sono XBGR8888 e ABGR8888: l'ordine dei canali
# e' l'opposto. SurfaceFlinger chiama present una trentina di volte al secondo,
# drm_hwcomposer prova drmModeAddFB2, il kernel dice no, e non esiste alcun
# framebuffer da mettere in scanout: sullo schermo resta la console del kernel.
#
# ENOENT e' fuorviante -- sembra un oggetto mancante, e porto' a sospettare handle
# GEM non validi, cache del compositore e permessi di DRM master, tutte ipotesi
# cadute con misure.
#
# Perche' nel kernel e non in Android: forzare BGRA a SurfaceFlinger non si puo'
# (nessuna proprieta' pubblica), e scambiare i canali nel compositore darebbe rosso
# e blu invertiti. Il protocollo virtio-gpu conosce gia' i formati giusti
# (R8G8B8X8_UNORM = 134, R8G8B8A8_UNORM = 67): manca solo il collegamento.
#
# IDEMPOTENTE: rieseguirlo su un albero gia' modificato non fa nulla e esce 0.
set -eu

ALBERO="${1:-}"
if [ -z "$ALBERO" ] || [ ! -d "$ALBERO/drivers/gpu/drm/virtio" ]; then
    echo "uso: $0 <albero-del-kernel>"
    echo "     esempio: $0 \$HOME/linux-6.18.35"
    exit 1
fi

PLANE="$ALBERO/drivers/gpu/drm/virtio/virtgpu_plane.c"
DISPLAY="$ALBERO/drivers/gpu/drm/virtio/virtgpu_display.c"
for f in "$PLANE" "$DISPLAY"; do
    [ -f "$f" ] || { echo "FERMO: manca $f"; exit 1; }
done

fatto=0

# --- 1. i due formati nell'elenco del piano primario -------------------------
if grep -q 'DRM_FORMAT_XBGR8888' "$PLANE"; then
    echo "  1/3 elenco dei formati: gia' presente"
else
    python3 - "$PLANE" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
a = "static const uint32_t virtio_gpu_formats[] = {\n\tDRM_FORMAT_HOST_XRGB8888,\n};"
b = ("static const uint32_t virtio_gpu_formats[] = {\n"
     "\tDRM_FORMAT_HOST_XRGB8888,\n"
     "\t/* winq: l'ordine dei canali di Android (RGBX_8888 / RGBA_8888). */\n"
     "\tDRM_FORMAT_XBGR8888,\n"
     "\tDRM_FORMAT_ABGR8888,\n"
     "};")
if a not in t:
    sys.exit("elenco dei formati non nella forma attesa: albero diverso da 6.18?")
open(p, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))
PY
    echo "  1/3 elenco dei formati: aggiunti XBGR8888 e ABGR8888"
    fatto=1
fi

# --- 2. la traduzione verso i formati dell'host -----------------------------
if grep -q 'VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM' "$PLANE"; then
    echo "  2/3 traduzione: gia' presente"
else
    python3 - "$PLANE" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
a = ("\tcase DRM_FORMAT_BGRA8888:\n"
     "\t\tformat = VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM;\n"
     "\t\tbreak;\n")
b = a + ("\tcase DRM_FORMAT_XBGR8888:\n"
         "\t\tformat = VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM;\n"
         "\t\tbreak;\n"
         "\tcase DRM_FORMAT_ABGR8888:\n"
         "\t\tformat = VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM;\n"
         "\t\tbreak;\n")
if a not in t:
    sys.exit("il case di BGRA8888 non e' nella forma attesa")
open(p, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))
PY
    echo "  2/3 traduzione: aggiunti i due case"
    fatto=1
fi

# --- 3. il controllo in fb_create -------------------------------------------
if grep -q 'DRM_FORMAT_XBGR8888' "$DISPLAY"; then
    echo "  3/3 controllo in fb_create: gia' presente"
else
    python3 - "$DISPLAY" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
a = ("\tif (mode_cmd->pixel_format != DRM_FORMAT_HOST_XRGB8888 &&\n"
     "\t    mode_cmd->pixel_format != DRM_FORMAT_HOST_ARGB8888)\n")
b = ("\tif (mode_cmd->pixel_format != DRM_FORMAT_HOST_XRGB8888 &&\n"
     "\t    mode_cmd->pixel_format != DRM_FORMAT_HOST_ARGB8888 &&\n"
     "\t    mode_cmd->pixel_format != DRM_FORMAT_XBGR8888 &&\n"
     "\t    mode_cmd->pixel_format != DRM_FORMAT_ABGR8888)\n")
if a not in t:
    sys.exit("il controllo in fb_create non e' nella forma attesa")
open(p, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))
PY
    echo "  3/3 controllo in fb_create: esteso ai due formati"
    fatto=1
fi

# --- verifica, sempre: e' il punto dello script ------------------------------
echo "=== verifica"
mancanti=0
for coppia in \
    "$PLANE:DRM_FORMAT_XBGR8888" \
    "$PLANE:DRM_FORMAT_ABGR8888" \
    "$PLANE:VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM" \
    "$PLANE:VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM" \
    "$DISPLAY:DRM_FORMAT_XBGR8888" \
    "$DISPLAY:DRM_FORMAT_ABGR8888"; do
    f="${coppia%%:*}"
    s="${coppia##*:}"
    if grep -q "$s" "$f"; then
        printf "  %-34s %s\n" "$s" "in $(basename "$f")"
    else
        printf "  %-34s MANCANTE in %s\n" "$s" "$(basename "$f")"
        mancanti=1
    fi
done
[ "$mancanti" -eq 0 ] || { echo "FERMO: la modifica non e' completa."; exit 1; }
[ "$fatto" -eq 1 ] && echo "=== modifica applicata" || echo "=== nulla da fare, era gia' applicata"
exit 0
