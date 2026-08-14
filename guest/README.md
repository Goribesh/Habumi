# Fase 1 — struttura di build ed esecuzione

Tutto ciò che serve per costruire ed eseguire lo stack si ottiene con un comando:

```powershell
powershell -ExecutionPolicy Bypass -File guest\scripts\build-all.ps1
```

È idempotente. Se manca qualcosa della toolchain, si ferma indicando il comando `pacman` da
eseguire invece di installarlo di nascosto: quei comandi richiedono elevazione e vanno visti.

## Cosa produce

```
runtime/                        autonomo, rigenerabile, git-ignored (~470 MB)
├── bin/
│   ├── qemu-system-aarch64.exe QEMU 11.0.3 ARM64 nativo, whpx + venus
│   ├── libvirglrenderer-1.dll  il nostro ponte: Venus + shim Win32
│   └── *.dll                   105 dipendenze da clangarm64/bin
├── share/qemu/                 firmware (edk2-aarch64-code.fd), keymap, ROM
└── vulkan/                     ICD dzn: driver Vulkan dell'host
```

**Perché una directory nostra e non l'installazione MSYS2.** QEMU importa
`libvirglrenderer-1.dll` per nome, e Windows la cerca nella directory dell'eseguibile prima che
nel PATH: non esiste una variabile d'ambiente per sostituire il ponte. Durante le sonde si è
copiata la DLL dentro `C:\msys64\clangarm64\bin`, cioè in un albero gestito da pacman — al primo
`pacman -Syu` la modifica sparirebbe, e nel frattempo ogni altro programma che usa quel QEMU
vedrebbe il nostro ponte. `assemble_runtime.py` copia invece QEMU e la chiusura delle sue
dipendenze in `runtime/`, e MSYS2 resta intatto.

Due dettagli che quella chiusura deve gestire, e che si scoprono solo sbagliandoli:

- **`libEGL.dll` e `libGLESv2.dll` non compaiono in nessuna tabella di import.** libepoxy le
  risolve a runtime con `LoadLibrary`, quindi `objdump -p` non le vede. Senza di esse QEMU muore
  di segfault appena `-display egl-headless` crea un contesto. Su questa piattaforma le fornisce
  **ANGLE**, che traduce GLES in D3D11: è il motivo per cui il GL dell'host è accelerato
  sull'Adreno senza bisogno della Mesa d3d12 in `research/mesa`.
- **l'indice delle DLL disponibili va costruito in minuscolo.** I nomi negli import non hanno un
  case garantito (`MFPlat.DLL`, `KERNEL32.dll`): un confronto sensibile al case fa sembrare di
  sistema una dipendenza reale, che quindi non viene copiata.

## Dipendenze esterne

| cosa | dove | perché non è nel repo |
|---|---|---|
| toolchain e QEMU | MSYS2 CLANGARM64 | pacchetti di sistema, ~1 GB |
| sorgenti del ponte | `research/winq-virgl/` (clone di `winq-emu-virglrenderer` ramo `alpha10`) | terze parti; le nostre modifiche stanno in `research/patches/` e sono versionate |
| ICD Vulkan host | `research/mesa/dzn/` | scaricato da `research/scripts/get-mesa.ps1` |
| immagini guest | `guest/images/` | grandi e riscaricabili |

Le patch in `research/patches/` **non sono opzionali**:

- `0001` — `impl_tss_dtor_invoke()` è dichiarata in stile K&R e clang 22 promuove
  `-Wstrict-prototypes` a errore sotto il `-Werror=pedantic` che il meson di virglrenderer
  imposta. Senza, non compila.
- `0002` — `create_eventfd` su Windows non poteva riuscire, e QEMU imbocca quel percorso da sé.
  Senza, ogni contesto Venus fallisce.

## Pilotare un guest

`guest/scripts/guest_console.py` esegue QEMU e ne guida la console seriale leggendo l'output,
invece di sparare comandi a tempo fisso. Gli scenari sono file di direttive; i percorsi sono
relativi alla radice del repo.

```powershell
python guest\scripts\guest_console.py guest\scripts\probe-venus.scenario `
    --qemu runtime\bin\qemu-system-aarch64.exe --log guest\logs\venus.log
```

Ogni passo riporta il tempo impiegato, e un passo che non arriva entro il suo timeout viene
mostrato con le ultime righe ricevute. Serve a capire *dove* si blocca senza attendere la somma
di tutte le pause: il boot di Alpine risulta 23,7 s contro i 65 s che si stavano attendendo a
vuoto.

Scenari presenti:

| file | domanda a cui risponde |
|---|---|
| `probe-venus.scenario` | la catena Venus arriva fino a dzn? |
| `probe-venus-debug.scenario` | quale ICD trova il loader del guest e cosa risponde |
| `probe-gl-chain.scenario` | la catena OpenGL è accelerata? |
| `probe-dri-inventory.scenario` | quali driver DRI ha il guest |
| `probe-virgl-why.scenario` | perché manca il driver virgl |

## Parametri che non si scelgono a caso

- **`hostmem` non oltre 256M.** `blob=on` richiede `hostmem`, ma con `hostmem=512M` il firmware
  del guest non trova alcun block device (`map: No mapping found`): il BAR non entra nella
  finestra MMIO PCI della macchina `virt` e impedisce il mapping degli altri device. Misurato su
  questa macchina; a 256M il guest boota.
- **`VK_DRIVER_FILES` non va impostata**, ed è controintuitivo. La chiave
  `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` è vuota, da cui sembrava che senza indicare un ICD il
  loader non trovasse nulla. Invece i driver di questa macchina si registrano **per device**: per
  default il loader espone tre device, fra cui l'Adreno con driver **Qualcomm nativo** a Vulkan
  1.3.295 e 147 estensioni, e due dzn a 1.2.350. Forzando un manifest dzn — il nostro o quello di
  sistema — `vkCreateInstance` sull'host fallisce sempre con `-1`, verificato anche fuori da QEMU.


  `runtime/vulkan/` resta nel runtime come ripiego esplicito, non come impostazione predefinita.

- **`WINQ_DEVICE_EXCLUDE=Direct3D12` serve, su questa macchina.** Dei tre device host, i due dzn
  bloccano indefinitamente in `vkDeviceWaitIdle` quando il guest distrugge il device, e un guest
  che enumera tutti i device — `vulkaninfo` lo fa — si appende per sempre. Il device con driver
  Qualcomm nativo completa invece l'intero ciclo di vita. Il ponte accetta anche
  `WINQ_DEVICE_KEEP=<testo>` per la selezione positiva.

- **`WINQ_DIAG=1` accende la diagnostica del ponte**, che scrive in
  `%LOCALAPPDATA%\winq-emu\virglrenderer.log`: nomi dei comandi Venus, batch del ring, fasi
  dello smontaggio del device, device host esposti al guest. È l'unico modo per capire dove si
  ferma qualcosa dentro il renderer — e va tenuto spento quando non serve, perché scrive una
  riga per comando.
- **niente `-nographic` insieme a `-display`.** `-nographic` imposta il display a `none` e
  vince sull'opzione precedente, per cui `virtio-gpu-gl-pci` viene rifiutato con *the display
  backend does not have OpenGL support enabled*. Per avere console seriale e GL insieme si usa
  `-serial stdio` (o `-serial tcp:` come fa `guest_console.py`).
