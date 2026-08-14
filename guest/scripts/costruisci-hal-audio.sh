#!/bin/bash
# costruisci-hal-audio.sh -- ricostruisce audio.primary.waydroid.so con un
# anello ALSA piu' grande.
#
# PERCHE'. L'anello da 1024 frame (21,3 ms) e' il TETTO di tutta la catena audio:
# il buffer host non puo' riempirsi oltre cio' che il guest consegna, quindi ogni
# riserva a monte era limitata da qui. Con throughput al 100% e ogni elemento
# dimensionato al pareggio, qualunque jitter diventava udibile.
# qemu/probe/sonda-alsa-guest.c ha verificato che il driver virtio-snd concede
# fino a 8192 frame (170,7 ms), otto prove su otto sul percorso diretto hw:0,0.
#
# RISULTATO MISURATO con i valori di questo script (4096/8):
#     period_size  256 -> 512    (5,33 -> 10,67 ms)
#     buffer_size 1024 -> 4096   (21,3 -> 85,3 ms)
#     delay       1536 -> 7680   (32 -> 160 ms di riserva totale)
#     AudioFlinger HAL frame count 1024 -> 4096
#
# PERCHE' NON UNA PATCH AL BINARIO. Tentata e FALLITA: il .so e' spogliato, la
# dimensione del buffer e' 1024 moltiplicato per la dimensione del frame e il
# compilatore piega quel prodotto in una tabella di salto sul formato. Patchati
# due siti da 4096 a 16384 byte: nessun effetto, perche' non erano quelli che
# governano lo stream di uscita. Dal sorgente c'e' UNA costante e si vede.
#
# LE DUE COSTANTI, in audio/audio_hw.c:
#     PLAYBACK_PERIOD_SIZE   1024 -> 4096   (l'anello INTERO in frame,
#                                            malgrado il nome dica "period")
#     PLAYBACK_PERIOD_COUNT     4 -> 8      (cosi' il periodo resta 512 frame
#                                            invece di diventare 1024)
#
# COME SI VERIFICA CHE IL SORGENTE SIA IL NOSTRO. Cinque stringhe caratteristiche
# del binario in esecuzione devono comparire nel sorgente scaricato -- lo script
# lo controlla e si ferma se no. Senza questo si costruirebbe una versione
# diversa da quella che l'immagine esegue, con un ABI possibilmente incompatibile.
#
# PERCHE' NON SI POSSONO USARE ABBOZZI DI INTESTAZIONI. struct audio_stream_out
# deve avere ESATTAMENTE il layout che AudioFlinger si aspetta: un abbozzo
# sbagliato produce un HAL che si carica e corrompe la memoria, non un errore.
# Servono le intestazioni vere, ed e' la parte con piu' attrito.
set -euo pipefail

LAVORO="${1:-}"
if [ -z "$LAVORO" ]; then
    echo "uso: $0 <cartella-di-lavoro> [frame] [periodi]"
    echo "     esempio: $0 /tmp/hal-audio 4096 8"
    exit 1
fi
FRAME="${2:-4096}"
PERIODI="${3:-8}"

# NDK: si passa in NDK, oppure si cerca sotto %LOCALAPPDATA%, che e la
# posizione predefinita di Android Studio su qualunque macchina Windows.
NDK="${NDK:-$(cygpath -u "${LOCALAPPDATA:-$HOME}" 2>/dev/null || echo "$HOME")/Android/Sdk/ndk/28.2.13676358}"
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
CC="$BIN/aarch64-linux-android31-clang.cmd"
STRIP="$BIN/llvm-strip.exe"
OBJDUMP="$BIN/llvm-objdump.exe"
[ -x "$CC" ] || { echo "NDK non trovato: $CC"; exit 1; }

mkdir -p "$LAVORO"
cd "$LAVORO"
SRC="$LAVORO/audio_hw.c"
A="$LAVORO/aosp"
INC="$LAVORO/inc"
mkdir -p "$A" "$INC/alsa"

echo "== 1. il sorgente dell'HAL"
if [ ! -f "$SRC.originale" ]; then
    curl -sSL -o "$SRC.originale" \
      "https://raw.githubusercontent.com/waydroid/android_hardware_waydroid/lineage-20/audio/audio_hw.c"
fi
echo "   $(stat -c %s "$SRC.originale") byte"

echo "== 2. e' il sorgente del binario che l'immagine esegue?"
# Cinque stringhe estratte dal .so in esecuzione. Se una manca, e' un'altra
# versione e l'ABI potrebbe non corrispondere.
for s in "waydroid.pulse_runtime_path" "/run/user/1000/pulse" \
         "Failed to open pcm_out after" "Audio HAL for Waydroid" \
         "cannot open pcm_out driver"; do
    grep -qF "$s" "$SRC.originale" || {
        echo "   MANCA la stringa: $s"
        echo "   Il sorgente non corrisponde al binario in uso. Mi fermo."
        exit 1
    }
done
echo "   cinque stringhe su cinque: corrisponde"

echo "== 3. le intestazioni AOSP (le vere, non abbozzi)"
prendi() {
    nome="$1"; url="$2"; shift 2
    [ -d "$A/$nome" ] && { echo "   $nome: presente"; return; }
    git clone -q --depth 1 --filter=blob:none --sparse "$url" "$A/$nome"
    ( cd "$A/$nome" && git sparse-checkout set "$@" >/dev/null 2>&1 )
    echo "   $nome: $(find "$A/$nome" -name '*.h' | wc -l) intestazioni"
}
# include_all e NON include: in libhardware i file sotto include/ sono
# collegamenti simbolici, che git su Windows scrive come TESTO -- e il
# compilatore ci inciampa con "expected identifier or '('".
prendi libhardware https://github.com/aosp-mirror/platform_hardware_libhardware.git include_all
prendi media       https://android.googlesource.com/platform/system/media audio/include audio_utils/include
prendi core        https://github.com/aosp-mirror/platform_system_core.git libcutils/include libsystem/include
prendi logging     https://android.googlesource.com/platform/system/logging liblog/include
prendi alsa        https://github.com/alsa-project/alsa-lib.git include

echo "== 4. asoundlib.h e version.h, che configure genererebbe"
cp "$A/alsa/include/"*.h "$INC/alsa/" 2>/dev/null || true
cat > "$INC/alsa/version.h" <<'EOF'
#ifndef __ALSA_VERSION_H
#define __ALSA_VERSION_H
#define SND_LIB_MAJOR 1
#define SND_LIB_MINOR 2
#define SND_LIB_SUBMINOR 9
#define SND_LIB_EXTRAVER 1000000
#define SND_LIB_VERSION ((SND_LIB_MAJOR<<16)|(SND_LIB_MINOR<<8)|SND_LIB_SUBMINOR)
#define SND_LIB_VERSION_STR "1.2.9"
#endif
EOF
{
    cat "$A/alsa/include/asoundlib-head.h"
    # ump.h e ump_msg.h PRIMA di seq*: seqmid.h usa snd_ump_endpoint_info_t, e
    # con l'ordine alfabetico non compila.
    for h in asoundef.h version.h global.h input.h output.h error.h conf.h \
             pcm.h rawmidi.h ump.h ump_msg.h timer.h hwdep.h control.h \
             mixer.h seq_event.h seq.h seqmid.h seq_midi_event.h; do
        [ -f "$INC/alsa/$h" ] && echo "#include <alsa/$h>"
    done
    cat "$A/alsa/include/asoundlib-tail.h"
} > "$INC/alsa/asoundlib.h"
echo "   asoundlib.h assemblato"

echo "== 5. le due costanti: $FRAME frame in $PERIODI periodi"
python3 - "$SRC.originale" "$SRC" "$FRAME" "$PERIODI" <<'PY'
import io, sys
sorg, dest, frame, periodi = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
with io.open(sorg, "r", encoding="utf-8", errors="replace", newline="") as f:
    t = f.read()
for vecchio, nuovo in (
    ("#define PLAYBACK_PERIOD_SIZE 1024",
     "#define PLAYBACK_PERIOD_SIZE %s" % frame),
    ("#define PLAYBACK_PERIOD_COUNT 4",
     "#define PLAYBACK_PERIOD_COUNT %s" % periodi),
):
    if t.count(vecchio) != 1:
        raise SystemExit("'%s' compare %d volte: il sorgente e' cambiato a "
                         "monte, va riletto invece di sostituito alla cieca"
                         % (vecchio, t.count(vecchio)))
    t = t.replace(vecchio, nuovo)
with io.open(dest, "w", encoding="utf-8", newline="\n") as f:
    f.write(t)
print("   sostituite entrambe")
PY

echo "== 6. le librerie del guest, per le voci DT_NEEDED"
# Senza di esse il .so si collega ma non dichiara cosa gli serve, e il
# caricatore di Android non apre libasound.
mkdir -p "$LAVORO/libs"
if [ ! -f "$LAVORO/libs/libasound.so" ]; then
    ADB="${ADB:-$(cd "$(dirname "$0")/../.." && pwd)/runtime/bin/adb.exe}"
    W=$(cd "$LAVORO/libs" && cmd //c cd 2>/dev/null | tr -d '\r' || echo "")
    for l in /vendor/lib64/libasound.so /system/lib64/libcutils.so \
             /system/lib64/liblog.so /system/lib64/libc++.so; do
        MSYS_NO_PATHCONV=1 "$ADB" pull "$l" "$W" >/dev/null 2>&1 || {
            echo "   impossibile estrarre $l: serve l'emulatore avviato"
            echo "   (oppure copiarle a mano in $LAVORO/libs)"
            exit 1
        }
    done
fi
echo "   $(ls "$LAVORO/libs" | wc -l) librerie"

echo "== 7. compilazione"
"$CC" -c -O2 -fPIC -Wall -o "$LAVORO/audio_hw.o" "$SRC" \
    -I"$INC" \
    -I"$A/libhardware/include_all" \
    -I"$A/media/audio/include" \
    -I"$A/media/audio/include/system" \
    -I"$A/media/audio_utils/include" \
    -I"$A/core/libcutils/include" \
    -I"$A/core/libsystem/include" \
    -I"$A/logging/liblog/include"

echo "== 8. collegamento e spoglio"
"$CC" -shared -o "$LAVORO/hal-nuovo.so" "$LAVORO/audio_hw.o" \
    -L"$LAVORO/libs" -lasound -lcutils -llog -lc++
"$STRIP" --strip-unneeded "$LAVORO/hal-nuovo.so"

echo "== 9. verifiche sul prodotto"
# HMI e' HAL_MODULE_INFO_SYM: senza quello AudioFlinger non riconosce il modulo,
# e il sintomo sarebbe "nessun audio" senza nominare la causa.
"$OBJDUMP" -T "$LAVORO/hal-nuovo.so" | grep -q "HMI" \
    && echo "   HMI presente" \
    || { echo "   HMI ASSENTE: AudioFlinger non caricherebbe il modulo"; exit 1; }
echo "   DT_NEEDED: $("$OBJDUMP" -p "$LAVORO/hal-nuovo.so" | grep -c NEEDED)"
echo "   dimensione: $(stat -c %s "$LAVORO/hal-nuovo.so") byte"

echo
echo "== FATTO: $LAVORO/hal-nuovo.so"
echo "Installazione:  guest/scripts/installa-hal-4096.sh"
echo "Verifica dopo l'avvio:"
echo "  adb shell cat /proc/asound/card0/pcm0p/sub0/hw_params  -> buffer_size $FRAME"
echo "  adb shell dumpsys media.audio_flinger | grep 'HAL frame count'"
