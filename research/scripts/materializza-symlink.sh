#!/bin/bash
# Sostituisce i symlink che git su Windows ha reso file di testo con una COPIA
# del bersaglio.
#
# IL GUASTO, e il modo in cui si presenta. Questa macchina non ha il privilegio
# nativo di symlink, quindi git materializza ogni voce di modo 120000 come file
# di testo che contiene il percorso del bersaglio. Il compilatore poi ci finisce
# dentro e dice cose che non nominano la causa:
#
#   ../include/android_stub/sync/sync.h:1:1: error: cannot use dot operator on a type
#       1 | ../android/sync.h
#
# Cioe' sta compilando la STRINGA "../android/sync.h" come se fosse codice.
#
# PERCHE' NON SI VEDEVA PRIMA. Sull'albero Windows i dieci symlink di Mesa
# cadono tutti fuori dalla compilazione (.clang-format, documenti, uno script
# di CI): l'unico che conta e' include/android_stub/sync/sync.h, e android_stub
# si costruisce solo nella cross verso Android. Trovato al primo
# tentativo di quella cross.
#
# PERCHE' UNA COPIA E NON UN LINK. Servirebbe la modalita' sviluppatore di
# Windows o i privilegi di amministratore, cioe' una modifica alla macchina per
# compilare un albero usa e getta. La copia non ha bisogno di niente, e l'albero
# di lavoro e' ricreabile: se il bersaglio cambia a monte si rilancia questo.
#
# IDEMPOTENTE: un file gia' materializzato non e' piu' di modo 120000 nell'indice
# ma il suo contenuto non e' piu' un percorso, quindi viene saltato.
#
# USO
#   bash research/scripts/materializza-symlink.sh <albero>
set -u

ALBERO="${1:-}"
if [ -z "$ALBERO" ] || [ ! -d "$ALBERO/.git" ] && [ ! -f "$ALBERO/.git" ]; then
    echo "USO: bash $0 <albero git>"
    echo "  esempio: bash $0 research/mesa-android"
    exit 1
fi
cd "$ALBERO" || exit 1

FATTI=0
SALTATI=0
git ls-files -s | awk '$1=="120000" {print $4}' | while read -r f; do
    [ -f "$f" ] || continue
    BERSAGLIO=$(head -1 "$f" 2>/dev/null)
    # Un file gia' materializzato contiene codice, non un percorso: se la prima
    # riga non nomina un file esistente accanto, non e' un symlink da sciogliere.
    DEST="$(dirname "$f")/$BERSAGLIO"
    if [ ! -f "$DEST" ]; then
        printf "  salto  %-52s (bersaglio assente o gia' materializzato)\n" "$f"
        SALTATI=$((SALTATI + 1))
        continue
    fi
    cp "$DEST" "$f"
    printf "  copio  %-52s <- %s\n" "$f" "$BERSAGLIO"
    FATTI=$((FATTI + 1))
done

echo "=== fatto (i conteggi stanno nella pipe, vedi le righe sopra)"
