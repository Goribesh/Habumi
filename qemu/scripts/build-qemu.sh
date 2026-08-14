#!/bin/bash
# Compila QEMU 11.0.3 in MSYS2 CLANGARM64, solo il target aarch64.
#
# Perche' dai sorgenti: il backend di display -display winq e' codice dentro QEMU,
# e a un binario pacchettizzato non si aggiunge. La versione e' fissata a quella
# che gia' usiamo (mingw-w64-clang-aarch64-qemu 11.0.3-1), cosi' l'unica variabile
# e' la nostra aggiunta, non un cambio di versione mascherato.
#
# Si compila SOLO aarch64-softmmu: gli altri target triplicherebbero il tempo
# senza servire a nulla, dato che questo progetto emula solo aarch64.
set -euo pipefail
VER="${QEMU_VERSION:-11.0.3}"
SRC="$HOME/qemu-$VER"
JOBS="${JOBS:-$(nproc)}"

# Toolchain sbagliata produce un binario che si compila senza errori ma non
# serve a nulla: MSVC su questa macchina non ha target ARM64, quindi senza
# l'ambiente CLANGARM64 attivo 'clang' o non esiste o e' quello di un altro
# sottosistema MSYS2 (MINGW64, UCRT64...) e produce codice x86_64.
if ! command -v clang >/dev/null 2>&1 || [ "$(clang -dumpmachine)" != "aarch64-w64-windows-gnu" ]; then
    echo "ERRORE: serve l'ambiente MSYS2 CLANGARM64 (clang -dumpmachine deve" >&2
    echo "rispondere aarch64-w64-windows-gnu). Lanciare con:" >&2
    echo "  MSYSTEM=CLANGARM64 /c/msys64/usr/bin/bash.exe -lc \"bash $0\"" >&2
    exit 1
fi

# 'diff' non fa parte della toolchain CLANGARM64: tests/qapi-schema/meson.build
# lo invoca durante 'configure' anche se --target-list esclude i test e anche
# se si costruisce solo qemu-system-aarch64.exe, perche' meson CONFIGURA tutta
# la cartella tests/ a prescindere da cosa verra' poi chiesto a ninja. Senza
# 'diff', configure fallisce a meta' con "ERROR: Program 'diff' not found or
# not executable" dopo aver gia' speso tempo a risolvere tutto il resto. Sta
# nel pacchetto msys 'diffutils' (senza prefisso mingw-w64-clang-aarch64-,
# perche' e' uno strumento dell'ambiente MSYS, non della cross-toolchain).
if ! command -v diff >/dev/null 2>&1; then
    echo "ERRORE: manca 'diff'. Installare con:" >&2
    echo "  pacman -S --needed diffutils" >&2
    exit 1
fi

if [ ! -d "$SRC" ]; then
    cd "$HOME"
    [ -f "qemu-$VER.tar.xz" ] || curl -fL -O "https://download.qemu.org/qemu-$VER.tar.xz"
    # NECESSARIO, non un'ottimizzazione: l'estrazione COMPLETA (senza questi
    # --exclude) FALLISCE su questa macchina, provato di persona con la
    # primissima versione di questo script, identica a quella del piano,
    # senza alcuna esclusione: "tar -xf" e' uscito con stato 2 elencando
    # symlink non creati sotto roms/ e tests/lcitool/. La causa: l'archivio
    # ha 72 symlink (42 in roms/, 16 in tests/, 7 in subprojects/, 7 in
    # rust/) e puntano tutti a file che ESISTONO nel tarball (es.
    # "alpine-322-prep.sh -> alpine-prep.sh") -- non sono penzolanti in se'.
    # Falliscono perche' su MSYS2, quando manca il privilegio nativo per
    # creare symlink (niente Developer Mode, utente non amministratore), la
    # creazione del symlink ripiega su un hard link, che richiede che il
    # bersaglio esista GIA' SUL DISCO nel momento in cui tar lo processa: e'
    # un problema di ORDINE nello stream dell'archivio, non di contenuto
    # mancante. I simlink di subprojects/ e rust/ non hanno mai fallito
    # nelle prove fatte (il loro bersaglio capita prima nello stream); quelli
    # di roms/ e tests/lcitool/ si, sistematicamente, perche' l'archivio li
    # scrive in quell'ordine ad ogni estrazione (e' deterministico, non va
    # a fortuna). Escludere quelle due cartelle e' anche un risparmio quasi
    # totale sui tempi (roms/ e' l'85% dei file dell'archivio: 71737 su
    # 84758), ma il motivo per cui l'esclusione c'e' e' che senza non si
    # estrae affatto, non che si estrae piu' lentamente.
    #
    # roms/ contiene i sorgenti per RICOSTRUIRE i firmware (SeaBIOS, u-boot,
    # edk2, skiboot...); i blob gia' compilati che QEMU usa a runtime stanno
    # in pc-bios/ e vengono estratti comunque. Verificato che roms/ non serve
    # a costruire qemu-system-aarch64.exe leggendo configure e i meson.build:
    # configure cita roms/SLOF solo dentro "if have_target s390x-softmmu",
    # mai eseguito con --target-list=aarch64-softmmu; pc-bios/meson.build e
    # tests/{functional,qtest}/meson.build usano "roms" come nome di
    # variabile meson, non come riferimento alla cartella.
    #
    # tests/lcitool/ e' la copia vendorizzata del progetto esterno
    # libvirt-ci, usata dalla pipeline GitLab per preparare le immagini CI
    # (docker/VM) su cui girano i test: non e' letta da meson/ninja per
    # costruire l'emulatore.
    set +e
    tar -xf "qemu-$VER.tar.xz" \
        --exclude="qemu-$VER/roms/*" \
        --exclude="qemu-$VER/tests/lcitool/*"
    tar_status=$?
    set -e
    if [ "$tar_status" -ne 0 ]; then
        echo "ERRORE: 'tar -xf' e' uscito con stato $tar_status. Con ogni" >&2
        echo "probabilita' sono altri symlink dell'archivio caduti nello" >&2
        echo "stesso problema di roms/ e tests/lcitool/ (vedi commento sopra:" >&2
        echo "hard link verso un bersaglio non ancora estratto, per mancanza" >&2
        echo "del privilegio nativo di symlink su MSYS2). Leggere l'elenco" >&2
        echo "sopra per capire quale altra cartella va esclusa e aggiungerla" >&2
        echo "agli --exclude. $SRC e' rimasta a meta': cancellarla a mano" >&2
        echo "prima di ritentare, altrimenti il prossimo avvio la trova gia'" >&2
        echo "presente e salta l'estrazione senza completarla." >&2
        exit 1
    fi
fi

cd "$SRC"
if [ ! -f build/build.ninja ]; then
    # Le opzioni rispecchiano cio' che serve al progetto: virglrenderer per il
    # ponte, opengl per il percorso GL, sdl per mantenere il confronto A/B con
    # -display sdl finche' -display winq non esiste ancora.
    #
    # --disable-tests: l'opzione NON ESISTE in QEMU 11.0.3 (ne' in configure
    # ne' in meson_options.txt: "ERROR: unknown option --disable-tests").
    # Non serve comunque, perche' chiediamo a ninja solo il target
    # qemu-system-aarch64.exe: i test non vengono costruiti a meno che non
    # si chieda esplicitamente "ninja test".
    #
    # --disable-guest-agent: qga (QEMU Guest Agent, il servizio Windows per
    # l'integrazione host<->guest) richiede windmc per compilare il suo
    # messaggio di risorse .mc. windmc non esiste in NESSUN repo MSYS2
    # (verificato con "pacman -F windmc" dopo "pacman -Fy" su tutti i repo:
    # nessun risultato), quindi con guest-agent lasciato al suo default
    # "auto" configure fallisce con "ERROR: Program 'windmc' not found or
    # not executable" dentro qga/meson.build. Non ci serve: costruiamo solo
    # l'emulatore, non il servizio guest.
    #
    # --prefix=/clangarm64 --bindir=bin --with-suffix=qemu: SENZA QUESTI
    # l'avvio fallisce con "failed to find romfile "efi-virtio.rom"" anche
    # se il file c'e' in runtime/share/qemu -- il sintomo inganna perche'
    # parla di un file presente. A runtime QEMU non usa percorsi assoluti
    # compilati: ricalcola il RELATIVO fra CONFIG_BINDIR e
    # CONFIG_QEMU_DATADIR e lo applica alla cartella dell'eseguibile.
    #
    # Il colpevole e' configs/meson/windows.txt, un native-file che meson
    # carica sempre su Windows e che impone la convenzione "tutto in una
    # cartella sola" tipica dei programmi Windows: "bindir = ''" e
    # "qemu_suffix = ''". Con bindir vuoto il relativo verso share/ e' solo
    # "share/", senza "..": cerca in runtime/bin/share invece che in
    # runtime/bin/../share = runtime/share. --prefix da solo non basta a
    # correggerlo, perche' quei due valori restano vuoti qualunque sia il
    # prefisso finche' non li si sovrascrive esplicitamente (il file stesso
    # lo dice: "can still be overridden on the command line").
    #
    # --bindir=bin e --with-suffix=qemu sono le opzioni di configure che
    # sovrascrivono quei due default (viste in scripts/meson-buildoptions.sh
    # come -Dbindir=bin e -Dqemu_suffix=qemu), e ricreano il layout MSYS2
    # bin/ accanto a share/qemu/ -- lo stesso che assemble_runtime.py
    # riproduce in runtime/. --prefix=/clangarm64 resta comunque necessario:
    # senza, il prefisso finisce dentro build/pyvenv (il venv Python di
    # configure) invece che nel prefisso reale del pacchetto MSYS2. Non si
    # esegue mai "ninja install", quindi questi tre valori servono solo ai
    # percorsi compilati dentro il binario: nessun file di pacman viene
    # toccato o sovrascritto.
    ./configure \
        --prefix=/clangarm64 \
        --bindir=bin \
        --with-suffix=qemu \
        --target-list=aarch64-softmmu \
        --enable-virglrenderer \
        --enable-opengl \
        --enable-sdl \
        --enable-whpx \
        --disable-docs \
        --disable-guest-agent
fi
# Idempotente per costruzione: se build/build.ninja esiste gia', configure non
# viene rieseguito e ninja ricompila solo i file cambiati rispetto all'ultima
# corsa, non l'intero albero.
ninja -C build qemu-system-aarch64.exe -j "$JOBS"
ls -l build/qemu-system-aarch64.exe
./build/qemu-system-aarch64.exe --version | head -1
echo "=== QEMU-COMPILATO"
