# Compilare QEMU 11.0.3 dai sorgenti

Cancello del piano host-window: prima di scrivere il backend di display
`-display winq` (codice dentro QEMU, non aggiungibile a un binario
pacchettizzato), bisognava dimostrare che una nostra build dai sorgenti,
**senza alcuna modifica**, avvia Android come il binario di pacman
(`mingw-w64-clang-aarch64-qemu 11.0.3-1`). Questo documento registra come,
con quali dipendenze, in quanto tempo, e le trappole incontrate.

Script: `qemu/scripts/build-qemu.sh`.

## Toolchain

MSYS2 **CLANGARM64** (non MINGW64/UCRT64/CLANG64): e' l'unico sottosistema
con un compilatore che produce codice ARM64 nativo su questa macchina, dato
che MSVC qui non ha target ARM64. Verificato con:

```
clang -dumpmachine   ->  aarch64-w64-windows-gnu
```

Lanciare lo script sempre cosi':

```
MSYSTEM=CLANGARM64 /c/msys64/usr/bin/bash.exe -lc "bash qemu/scripts/build-qemu.sh"
```

## Dipendenze pacman

La maggior parte era gia' installata come dipendenza del pacchetto
`mingw-w64-clang-aarch64-qemu` di pacman (virglrenderer, SDL2, glib2,
pixman, spice, libslirp, usbredir, libepoxy, angleproject, meson, ninja,
python). **Una sola dipendenza mancava** ed e' andata installata a mano:

```
pacman -S --needed diffutils
```

`diffutils` sta nel repo **msys**, senza prefisso
`mingw-w64-clang-aarch64-`, perche' fornisce `diff`, uno strumento
dell'ambiente MSYS e non della cross-toolchain. Serve perche'
`tests/qapi-schema/meson.build:226` invoca `diff` durante `configure`
**anche se non si costruira' mai `tests/`**: meson configura quella
cartella a prescindere da quali target verranno poi chiesti a `ninja`.
Senza, `configure` falliva a meta' con:

```
../tests/qapi-schema/meson.build:226:7: ERROR: Program 'diff' not found or not executable
```

Lo script verifica `diff` in precheck, prima di scaricare o estrarre
qualunque cosa, cosi' l'errore si vede subito e non dopo dieci minuti di
estrazione.

## Le opzioni di `configure` e perche'

```
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
```

- **`--target-list=aarch64-softmmu`**: l'unico target che serve. Gli altri
  triplicherebbero il tempo di compilazione senza motivo, dato che questo
  progetto emula solo aarch64.
- **`--enable-virglrenderer` `--enable-opengl` `--enable-sdl`**: quello che
  serve al progetto — virglrenderer per il ponte Venus, opengl per il
  percorso GL, sdl per mantenere il confronto A/B con `-display sdl` finche'
  `-display winq` non esiste.
- **`--enable-whpx`**: l'acceleratore hypervisor che il progetto usa
  (`-accel whpx`).
- **`--disable-docs`**: non servono documenti generati per una build di
  validazione.
- **`--disable-guest-agent`**: **non opzionale su questa toolchain**. Il
  QEMU Guest Agent (`qga`, il servizio Windows per l'integrazione
  host<->guest) richiede `windmc` per compilare la sua risorsa di
  messaggi `.mc`. `windmc` **non esiste in nessun repo MSYS2** — verificato
  con `pacman -F windmc` dopo un `pacman -Fy` su tutti i repo (clangarm64,
  ucrt64, clang64, mingw32, mingw64, msys): nessun risultato. Con
  guest-agent lasciato al suo default `auto`, `configure` falliva con:
  ```
  ../qga/meson.build:107:11: ERROR: Program 'windmc' not found or not executable
  ```
  Non ci serve comunque: costruiamo l'emulatore, non il servizio guest.
- **`--prefix=/clangarm64` `--bindir=bin` `--with-suffix=qemu`**: la
  trappola piu' insidiosa delle tre, perche' il sintomo mente. Vedi sotto.

### Una opzione che non esiste

Una prima stesura di queste istruzioni elencava `--disable-tests` fra le
opzioni. **Quella
opzione non esiste in QEMU 11.0.3**: ne' `configure --help` ne'
`meson_options.txt` la conoscono, e passarla produce
`ERROR: unknown option --disable-tests`. Non serve comunque, perche' si
chiede a `ninja` solo il target `qemu-system-aarch64.exe`: i test non si
costruiscono a meno di chiedere esplicitamente `ninja test`. Il piano e'
stato corretto per rimuoverla; questo script non la usa.

## La trappola del romfile: perche' servono `--prefix`, `--bindir`, `--with-suffix` insieme

**Sintomo**, all'avvio di Android con la build compilata solo con
`--prefix=/clangarm64` (senza gli altri due):

```
qemu-nostro.exe: -device virtio-net-pci,netdev=n0: failed to find romfile "efi-virtio.rom"
```

Il sintomo **inganna**: il file c'e' davvero, in `runtime/share/qemu/`
(copiato li' da `assemble_runtime.py`, che riproduce il layout MSYS2
`bin/` accanto a `share/qemu/`). Il problema e' dove il binario lo cerca.

A runtime QEMU non usa i percorsi assoluti compilati dentro il binario:
ricalcola il **relativo** fra `CONFIG_BINDIR` e `CONFIG_QEMU_DATADIR` e lo
applica alla cartella dove si trova l'eseguibile. Il colpevole e'
`configs/meson/windows.txt`, un native-file che meson carica sempre su
Windows e che impone la convenzione "tutto in una cartella sola" tipica
dei programmi Windows:

```
[built-in options]
bindir = ''

[project options]
qemu_suffix = ''
```

Con `bindir` vuoto, il relativo verso `share/` e' solo `share/`, senza
`..`: cerca in `runtime/bin/share` invece che in
`runtime/bin/../share` = `runtime/share`. **`--prefix` da solo non basta**:
quel file dice esplicitamente "can still be overridden on the command
line", ma finche' non li si sovrascrive esplicitamente quei due valori
restano vuoti qualunque sia il prefisso. Con solo `--prefix=/clangarm64`
(senza gli altri due) i define compilati erano:

```
CONFIG_BINDIR        "C:/msys64/clangarm64/."
CONFIG_QEMU_DATADIR  "C:/msys64/clangarm64/share/"
```

`bindir` ancora `.`, datadir senza il suffisso `qemu`: stesso sintomo.

**Correzione**: `--bindir=bin --with-suffix=qemu` (viste in
`scripts/meson-buildoptions.sh` come `-Dbindir=bin` e
`-Dqemu_suffix=qemu`) sovrascrivono quei due default e ricreano il layout
MSYS2. Con tutte e tre le opzioni:

```
CONFIG_BINDIR        "C:/msys64/clangarm64/bin"
CONFIG_QEMU_DATADIR  "C:/msys64/clangarm64/share/qemu"
```

Relativo `../share/qemu`: quello giusto. Non si esegue mai
`ninja install`, quindi questi tre valori servono solo ai percorsi
compilati dentro il binario — nessun file di pacman viene toccato.

**Come verificarlo in un secondo, senza sprecare un ciclo di boot**:

```
runtime/bin/qemu-nostro.exe -L help
```

Deve stampare `...runtime\bin/../share/qemu` e
`...runtime\bin/../share/qemu-firmware`, identico all'output del binario di
pacman. Misurato su questa build:

```
C:\...\runtime\bin/../share/qemu-firmware
C:\...\runtime\bin/../share/qemu
```

**Trappola nella verifica stessa**: eseguire `-L help` sul binario
**dentro** `build/` (es. `build/qemu-system-aarch64.exe -L help`) stampa
`build/qemu-bundle/msys64/clangarm64/share/qemu` — un meccanismo di
comodo di QEMU per provare l'eseguibile senza installare, attivo solo
quando gira dal proprio albero di compilazione. Non e' un errore e non
dice nulla sul layout che avra' la copia in `runtime/`: il controllo va
fatto sempre sulla copia effettivamente installata in `runtime/bin/`.

## L'esclusione di `roms/` e `tests/lcitool/`: necessaria, non un'ottimizzazione

**L'estrazione COMPLETA del tarball, senza alcuna esclusione, fallisce su
questa macchina.** Provato direttamente: la primissima versione di questo
script (identica alle istruzioni originali del piano, `tar -xf` senza
`--exclude`) e' uscita con stato 2, con ventiquattro simlink non creati
sotto `roms/` e `tests/lcitool/`:

```
tar: qemu-11.0.3/roms/u-boot-sam460ex/COPYING: impossibile creare un collegamento
     simbolico a "Licenses/gpl-2.0.txt": No such file or directory
tar: qemu-11.0.3/tests/lcitool/libvirt-ci/ci/gitlab/all_mappings_prep_env/alpine-322-prep.sh:
     impossibile creare un collegamento simbolico a "alpine-prep.sh": No such file or directory
[...altre 22 righe simili...]
tar: Uscita con stato di fallimento in base agli errori precedenti
```

Con `set -e` attivo, quello stato di uscita fermava lo script prima
ancora di arrivare a `configure`. **Le esclusioni non sono quindi
un'ottimizzazione di velocita': senza, lo script non funziona affatto per
nessuno su una macchina Windows senza il privilegio nativo di symlink.**

Causa, verificata leggendo l'archivio (`tar -tvf`): il tarball ha **72
symlink** (42 in `roms/`, 16 in `tests/`, 7 in `subprojects/`, 7 in
`rust/`), e puntano tutti a file che **esistono davvero** nel tarball —
non sono symlink penzolanti in se'. Falliscono perche' su MSYS2, quando
manca il privilegio nativo per creare symlink (niente Developer Mode
attivo, utente non amministratore — verificato con
`fsutil behavior query SymlinkEvaluation` e l'assenza della chiave di
registro `AllowDevelopmentWithoutDevicePrivilege`), la creazione del
symlink ripiega su un **hard link**, che richiede che il bersaglio esista
gia' sul disco nel momento in cui `tar` lo processa. E' un problema di
**ordine nello stream dell'archivio**, non di contenuto mancante: i
symlink di `subprojects/` e `rust/` non hanno mai fallito nelle prove
fatte (il loro bersaglio capita prima nello stream), quelli di `roms/` e
`tests/lcitool/` si, sistematicamente, perche' l'archivio li scrive in
quell'ordine a ogni estrazione — deterministico, non a fortuna.

Perche' e' comunque sicuro escluderle dal punto di vista del build:

- **`roms/`** contiene i *sorgenti* per ricostruire i firmware (SeaBIOS,
  u-boot, edk2, skiboot...). I blob gia' compilati che QEMU usa a runtime
  stanno in `pc-bios/` e vengono estratti comunque. Verificato leggendo
  `configure` e i `meson.build`: `configure:1734` cita `roms/SLOF` solo
  dentro `if have_target s390x-softmmu && probe_target_compiler
  s390x-softmmu`, un ramo mai eseguito con `--target-list=aarch64-softmmu`;
  `pc-bios/meson.build` e `tests/{functional,qtest}/meson.build` usano
  `roms` come nome di **variabile meson**, non come riferimento alla
  cartella.
- **`tests/lcitool/`** e' la copia vendorizzata del progetto esterno
  `libvirt-ci`, usata dalla pipeline GitLab per preparare le immagini CI
  (docker/VM) su cui girano i test upstream: non e' letta da meson/ninja
  per costruire l'emulatore.

**Verificato end-to-end, non solo per analisi statica**: con l'albero
sorgente cancellato e ricreato da zero (`rm -rf $HOME/qemu-11.0.3`, poi
`bash qemu/scripts/build-qemu.sh` senza alcun intervento manuale),
l'estrazione con entrambe le esclusioni e' andata a buon fine (nessun
errore da `tar`), `configure` ha scritto `build/build.ninja`, e `ninja` ha
prodotto `qemu-system-aarch64.exe` che poi ha avviato Android con
successo (vedi sezione Avvio). Non e' quindi solo una deduzione da
lettura dei sorgenti: lo script finito, cosi' com'e', compila da un
albero pulito.

**Nota metodologica su un tentativo intermedio**: durante la messa a
punto, un run che aveva escluso solo `roms/` (non ancora `tests/lcitool/`)
e' fallito con `tar` in stato 2 e ha lasciato un albero parziale da
12.240 file. Proseguire da li' senza rifare l'estrazione (idea scartata)
avrebbe dato in seguito errori che non nominano la causa: quell'albero
sembrava utilizzabile (`configure` e file principali presenti) ma non lo
era in modo verificabile a colpo d'occhio. La corsa pulita finale, con
entrambe le esclusioni, ha prodotto 16.718 file — piu' del tentativo
parziale, a conferma che quello si era davvero fermato incompleto. Per
questo lo script ora cattura esplicitamente il codice di uscita di `tar`
(non si affida solo a `set -e`) e, se fallisce, dice la causa probabile e
istruisce a cancellare a mano l'albero parziale prima di ritentare,
invece di lasciare che una prossima esecuzione lo trovi e lo dia per
buono.

## Tempi misurati

Macchina: Surface Pro 11, Snapdragon X, 10 core logici (`nproc` = 10).

| fase | con `roms/` + `tests/lcitool/` | senza (esclusioni attive) |
|---|---|---|
| estrazione tarball | fallisce (vedi sopra) | inclusa nel totale sotto |
| intero script (estrazione + configure + ninja), da tarball gia' scaricato | n/d (non completa) | **3 min 42 s** (14:11:23 -> 14:15:05) |
| solo `ninja -C build qemu-system-aarch64.exe -j 10` | 112 s (misurato) | 112 s e 105 s (due misure separate, stessa configurazione a parte i tre path fix) |

Conteggio file dell'archivio (`tar -tf qemu-11.0.3.tar.xz`): **84.758**
elementi totali, di cui **71.737** sotto `roms/` (84,6%) e **413** sotto
`tests/lcitool/`. Albero estratto senza le due cartelle (`find -type f`):
**16.718** file su disco, **0** sotto `roms/`.

Dimensione tarball: 141.366.336 byte. Eseguibile prodotto:
115.879.936 byte, PE `coff-arm64` / `machine 0xaa64` (ARM64 nativo,
confermato con `objdump -f`).

`ninja`: 2072 azioni totali (`[2072/2072]`), stesso conteggio in tutte e
tre le compilazioni fatte durante la messa a punto.

## Verifica dell'equivalenza col binario di pacman

```
$ runtime/bin/qemu-nostro.exe --version | head -1
QEMU emulator version 11.0.3
```

Identica al binario di pacman (`mingw-w64-clang-aarch64-qemu 11.0.3-1`).

```
$ runtime/bin/qemu-nostro.exe -display help
none gtk sdl egl-headless curses spice-app dbus
```

`sdl` presente, `winq` assente: lo stato di partenza atteso prima di
scrivere il backend.

## Copia nel runtime

```
cp "$HOME/qemu-11.0.3/build/qemu-system-aarch64.exe" runtime/bin/qemu-nostro.exe
```

Chiusura delle dipendenze calcolata con lo stesso algoritmo (BFS su
`objdump -p`, in ampiezza, solo DLL presenti in `clangarm64/bin`) usato da
`guest/scripts/assemble_runtime.py`: **103 DLL** nella chiusura, **tutte
gia' presenti** in `runtime/bin` (stesse librerie del binario di pacman,
stessa configurazione di feature). Nessuna DLL da copiare in piu'.

**Avvertenza per il seguito del piano**: `configure` ha trovato
**virglrenderer 1.3.0 di pacman** (`mingw-w64-clang-aarch64-virglrenderer`),
non il fork del progetto `winq-emu-virglrenderer@alpha10`. Per questo task
e' irrilevante (si sta solo provando l'equivalenza del binario QEMU), ma
quando si scrivera' il backend `-display winq` e si vorra' Venus dal
nostro ponte, il runtime dovra' caricare la nostra
`libvirglrenderer-1.dll` e non quella di sistema — se carica quella di
sistema, Venus sparisce senza che nulla lo dica (stesso principio per cui
`assemble_runtime.py` copia il ponte *dopo* la chiusura delle dipendenze,
per sovrascriverlo).

## Prova d'avvio

```
powershell -NoProfile -ExecutionPolicy Bypass -File "guest\scripts\avvia-android.ps1" -Eseguibile runtime/bin/qemu-nostro.exe
```

`avvia-android.ps1` ha ricevuto un nuovo parametro `-Eseguibile` (default
`runtime/bin/qemu-system-aarch64.exe`, il binario di pacman, che resta il
termine di confronto A/B) usato in `Start-Process -FilePath`; nessun'altra
struttura dello script e' stata toccata.

Risultato:

```
tentativo 1: QEMU pid 31632
  avvio sano: 1314 righe di seriale

AVVIO COMPLETATO. La finestra e' interattiva.
```

```
$ adb -s 127.0.0.1:5555 shell getprop sys.boot_completed
1
```

Oltre al criterio minimo richiesto, verificato anche che il guest arriva
al launcher e che la catena GPU regge sul ponte virgl:

```
ro.hardware.hwcomposer  = drm
GLES                    = Mesa, virgl (D3D12 (Qualcomm(R) Adreno(TM) X1-85 GPU)), OpenGL ES 3.1 Mesa 26.0.1
mCurrentFocus           = Window{... com.android.launcher3/...QuickstepLauncher}
```

**Trappola nella verifica con adb**: dopo l'avvio, `adb devices` vede
**due** dispositivi:

```
127.0.0.1:5555   device
emulator-5554    device
```

Un comando senza `-s` muore con `adb.exe: more than one device/emulator`,
e il messaggio non suggerisce che la soluzione e' scegliere il
dispositivo. Usare sempre `adb -s 127.0.0.1:5555 ...`.

## Idempotenza

Rieseguire `bash qemu/scripts/build-qemu.sh`:
- non riscarica il tarball se `$HOME/qemu-11.0.3.tar.xz` esiste gia';
- non ri-estrae se `$HOME/qemu-11.0.3` esiste gia' (per questo, se `tar`
  fallisce a meta', lo script chiede di cancellare a mano l'albero
  parziale prima di ritentare, invece di lasciare che il controllo
  `[ ! -d "$SRC" ]` lo dia per buono cosi' com'e');
- non ri-esegue `configure` se `build/build.ninja` esiste gia';
- `ninja` ricompila solo i file cambiati rispetto all'ultima corsa, non
  l'intero albero.

## File cambiati per questo task

- `qemu/scripts/build-qemu.sh` (nuovo)
- `qemu/QEMU-BUILD.md` (nuovo, questo file)
- `guest/scripts/avvia-android.ps1` (parametro `-Eseguibile`)
- `runtime/bin/qemu-nostro.exe` (nuovo, non tracciato se il repo esclude i
  binari di runtime — verificare `.gitignore`)
