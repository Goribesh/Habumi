#!/bin/bash
# Ricrea guest/images/android/data.img come ext4 vuota.
#
# PERCHE' ESISTE. Il /data del guest si e' disallineato dall'immagine di
# sistema: 66 pacchetti con l'uid sbagliato, e PackageManager che per ognuno
# dichiara "I am not changing its files so it will probably fail". Il sintomo
# e' un'interfaccia che a ogni avvio perde un pezzo diverso -- una volta la
# tendina delle notifiche nera, una volta il launcher senza icone -- e rende
# inutilizzabile qualunque verifica visiva. Succede conservando /data
# attraverso un cambio di system.img.
#
# DA ESEGUIRE DENTRO WSL, perche' l'host non ha mkfs.ext4:
#   wsl -d Ubuntu-26.04 -- bash /mnt/c/.../guest/scripts/ricrea-data-img.sh
#
# La VM deve essere SPENTA: su Windows il file resta bloccato finche' QEMU
# vive, e "tasklist | grep" non e' un controllo affidabile (l'ho visto dare
# zero con i processi ancora vivi). Meglio verificarlo aprendo il file.
set -euo pipefail

# La radice del progetto si deriva da dove sta QUESTO file: due livelli sopra
# guest/scripts. Funziona sia in MSYS sia in WSL, perche e il percorso con cui
# lo script e stato invocato -- un percorso assoluto scritto a mano girerebbe
# su una macchina sola.
RADICE="$(cd "$(dirname "$0")/../.." && pwd)"
IMG="$RADICE/guest/images/android/data.img"
DIM=4294967296   # 4 GiB, la stessa dell'immagine precedente

if [ -e "$IMG" ]; then
    echo "esiste gia': $IMG"
    echo "spostalo o cancellalo prima, questo script non sovrascrive."
    exit 1
fi

echo "creo $IMG da $DIM byte"
truncate -s "$DIM" "$IMG"

# ^orphan_file e ^metadata_csum_seed: e2fsprogs recenti li accendono di serie,
# ma il kernel del guest e' di un Android 13 e potrebbe rifiutarli in fase di
# mount. L'initramfs non formatta e si FERMA se data.img non e' montabile
# ("WINQ-FERMO: data.img non montabile"), quindi un mount rifiutato non degrada:
# blocca l'avvio.
mkfs.ext4 -F -q -L data -O '^orphan_file,^metadata_csum_seed' "$IMG"

echo "fatto:"
ls -l "$IMG"
file "$IMG"
