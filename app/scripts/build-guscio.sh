#!/bin/bash
# app/scripts/build-guscio.sh -- compila Habumi.exe.
#
# In MSYS2 CLANGARM64, con percorso assoluto:
#   MSYSTEM=CLANGARM64 /c/msys64/usr/bin/bash.exe -lc "bash <radice-del-progetto>/app/scripts/build-guscio.sh"
#
# -mwindows: nessuna finestra di console all'avvio. Il guscio ha la propria
# finestra, e una console che lampeggia dietro sarebbe solo confusione.
set -euo pipefail
cd "$(dirname "$0")/../guscio"

# -lws2_32 e' per Winsock, che serve al canale degli appunti condivisi
# (appunti.c). Senza, il collegamento fallisce con dei simboli non risolti --
# socket, bind, select -- e non con un messaggio che nomini la libreria.
#
# -lwinhttp e -lbcrypt sono per scarica.c: la rete (con HTTPS e la verifica
# del certificato inclusi) e l'impronta sha256 dell'immagine scaricata. Sono
# gia' dentro Windows -- nessuna dipendenza nuova da installare -- ma vanno
# collegate esplicitamente come ws2_32, o il link fallisce sui simboli
# WinHttp*/BCrypt* invece che su un errore che nomini la libreria.
#
# QUESTO ELENCO VA TENUTO IN PARI A MANO, e le prove NON se ne accorgono.
# test-guscio.sh compila una suite per volta, e ogni suite dichiara i propri
# moduli: puo' quindi essere tutta verde mentre il PRODOTTO non collega. E'
# successo aggiungendo varianti.c, che config.c chiama: tredici
# suite verdi e questo comando fermo su
#     ld.lld: error: undefined symbol: var_da_testo
# Chi aggiunge un modulo al guscio aggiunge una riga qui, e lancia QUESTO
# comando oltre alle prove.
clang -std=c11 -Wall -Werror -O2 -mwindows \
      -o ../../runtime/bin/Habumi.exe \
      main.c finestra.c vm.c adb.c config.c registro.c tubo.c dpi.c apk.c \
      file.c rilascio.c archivio.c appunti.c appunti-trama.c varianti.c \
      dati.c scarica.c hyperv.c \
      -luser32 -lgdi32 -lcomctl32 -lshell32 -lws2_32 -lwinhttp -lbcrypt

echo "fatto: runtime/bin/Habumi.exe"
