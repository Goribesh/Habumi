#!/bin/bash
# app/scripts/test-guscio.sh -- le prove del guscio, senza VM.
#
# Va lanciato in MSYS2 CLANGARM64, non in Git Bash: 'clang' li' o non esiste o
# e' quello di un altro sottosistema. Con percorso assoluto, perche' quella
# shell parte dalla propria home:
#   MSYSTEM=CLANGARM64 /c/msys64/usr/bin/bash.exe -lc "bash <radice-del-progetto>/app/scripts/test-guscio.sh"
set -euo pipefail
cd "$(dirname "$0")/../guscio"

esito=0
for prova in registro config vm dpi adb apk file rilascio archivio appunti-trama varianti dati hyperv; do
    if [ ! -f "test-$prova.c" ]; then
        continue
    fi
    # LE DIPENDENZE SONO LA DOCUMENTAZIONE ESEGUIBILE DEI CONFINI: se un modulo
    # comincia a chiamare qualcosa che non e' nella sua riga, la prova non compila.
    # E' cosi' che "rilascio.c non dipende da vm.c" resta vero invece di restare
    # scritto. registro.c tira dentro archivio.c, quindi chi linka
    # il registro linka anche quello.
    extra="registro.c archivio.c"
    if [ "$prova" = "registro" ]; then extra="archivio.c"; fi
    # config.c chiama var_da_testo (varianti.c) dalla riga "variante": chi
    # linka config.c, direttamente o dentro il proprio extra, deve linkare
    # anche varianti.c, o il link fallisce con "undefined symbol: var_da_testo"
    # a prescindere da cosa la singola prova chiami davvero -- e' cosi' per
    # config stesso e per ognuna delle righe qui sotto che include config.c.
    if [ "$prova" = "config" ]; then extra="registro.c archivio.c varianti.c"; fi
    if [ "$prova" = "vm" ]; then extra="registro.c archivio.c config.c adb.c varianti.c"; fi
    if [ "$prova" = "dpi" ]; then extra="registro.c archivio.c config.c varianti.c"; fi
    if [ "$prova" = "adb" ]; then extra="registro.c archivio.c config.c varianti.c"; fi
    if [ "$prova" = "apk" ]; then extra="registro.c archivio.c config.c adb.c varianti.c"; fi
    # file.c e archivio.c non registrano e non eseguono: funzioni pure piu' due
    # chiamate a Win32, zero dipendenze dagli altri moduli del guscio.
    if [ "$prova" = "file" ]; then extra=""; fi
    if [ "$prova" = "archivio" ]; then extra=""; fi
    # appunti-trama.c e' PURO: niente Winsock, niente windows.h, niente moduli del
    # guscio -- solo <stdint.h> <stdbool.h> <string.h> <stddef.h>. Questa riga
    # vuota e' il confine reso eseguibile: il giorno che quel modulo chiamasse il
    # registro o aprisse un socket, la sua prova non compilerebbe. Il formato di
    # trama e la guardia contro l'eco si provano cosi' in millisecondi, senza VM
    # e senza emulatore, che e' l'unico modo per cui vengono provati davvero.
    if [ "$prova" = "appunti-trama" ]; then extra=""; fi
    # varianti.c e' PURO come appunti-trama.c: niente Win32, niente rete, nessun
    # modulo del guscio. Questa riga vuota e' il confine reso eseguibile.
    if [ "$prova" = "varianti" ]; then extra=""; fi
    # hyperv.c non dipende da NIENTE del guscio: carica una DLL di Windows e
    # legge un intero. Questa riga vuota e' il confine reso eseguibile -- il
    # giorno che quel modulo scrivesse nel registro, la sua prova non
    # compilerebbe, ed e' voluto: e' chiamato PRIMA che la finestra esista.
    if [ "$prova" = "hyperv" ]; then extra=""; fi
    # dati.c tocca Win32 (tar.exe, GetFileAttributesA, MoveFileExA) e ORA
    # anche registro.c: dati_sposta_con_riprova (condivisa con
    # variante_estrai_immagine in main.c, vedi il commento su di lei in
    # guscio.h) scrive nel registro ogni tentativo di spostamento fallito e
    # riprovato -- cosa che dati_comando_estrai e dati_cartella_temp, le sole
    # funzioni pure di questo modulo, non facevano. registro.c tira dentro
    # archivio.c (vedi il commento generale sopra), quindi entrambi vanno
    # linkati.
    if [ "$prova" = "dati" ]; then extra="registro.c archivio.c"; fi
    # rilascio.c registra, legge la Config, esegue adb e chiama i due moduli che
    # sanno installare e copiare. NON dipende da vm.c: lo stato gli arriva come
    # parametro, e per questo il guardiano si prova senza la macchina a stati.
    if [ "$prova" = "rilascio" ]; then extra="registro.c archivio.c config.c adb.c apk.c file.c varianti.c"; fi
    clang -std=c11 -Wall -Werror -o "test-$prova.exe" "test-$prova.c" "$prova.c" $extra
    ./"test-$prova.exe" || esito=1
done
exit $esito
