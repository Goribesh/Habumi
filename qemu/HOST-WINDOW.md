# host-window: il backend `-display winq`

Un backend di display dentro QEMU, scritto per questo progetto, che presenta i frame del guest Android su una finestra Win32 nativa ARM64 e traduce tocco, tastiera e mouse in eventi che Android accetta.

Sorgenti in `qemu/ui-winq/`, innestati nell'albero di QEMU da `qemu/scripts/innesta-winq.sh`.

## Perché esiste

Android girava già, arrivava al launcher e disegnava sull'Adreno X1-85 attraverso il ponte virglrenderer. Ma la finestra era quella di QEMU col backend SDL, e **SDL non consegna al guest il tocco**. Misurato con `getevent` dentro il guest:

| evento | conteggio | esito |
|---|---|---|
| `EV_ABS ABS_X` / `ABS_Y` | 864 / 952 | arrivano al kernel |
| `EV_KEY BTN_LEFT` | 84 | arriva |
| `EV_KEY BTN_TOUCH` | **0** | mai inviato |

Android classifica un dispositivo con assi assoluti in base alla presenza di `BTN_TOUCH` fra le capacità, e ogni mapper adatto pretende quell'evento per stabilire che il dito è giù. Risultato osservato: clic registrati, cursore immobile.

**Il difetto non era in QEMU.** `hw/input/virtio-input-hid.c:34` mappa già `[INPUT_BUTTON_TOUCH] = BTN_TOUCH`, e `virtio_multitouch_init` (righe 506-509) dichiara `ABS_MT_SLOT`, `ABS_MT_TRACKING_ID`, `ABS_MT_POSITION_X/Y`. Era **SDL che non chiamava mai quel percorso**. Il pezzo mancante stava dalla nostra parte, e questo backend è quel pezzo.

## Cosa funziona, e con quale prova

| cosa | prova |
|---|---|
| Android avvia fino al launcher nella nostra finestra | `sys.boot_completed=1`, `QuickstepLauncher` a fuoco, launcher **catturato** |
| presentazione senza SDL | `frame 1 (superficie 2D)` → `frame 10, 100 (scanout)`: i due percorsi nell'ordine giusto |
| texture del ponte usabile senza copie | `glIsTexture=1`, FBO `COMPLETE`, `glGetError=0x0` — misurato dopo aver **svuotato** la coda errori |
| **tocco** | **37 `BTN_TOUCH` giù e 37 su**, in equilibrio, col dito dell'utente su un Surface Pro 11 |
| **tastiera** | lettere corrette e frecce operative, verificato sulla tastiera fisica |
| mappatura delle coordinate | 27 casi di prova, fra cui l'invariante disegno/tocco |
| `-display sdl` continua a funzionare | riverificato sul binario committato: mantiene `D3D12 … ES 3.1` |

## Come si compila

QEMU va compilato dai sorgenti: un backend di display è codice dentro QEMU, e al binario di pacman non si aggiunge.

```bash
# 1. QEMU 11.0.3 dai sorgenti, in MSYS2 CLANGARM64. Vedi qemu/QEMU-BUILD.md
#    per le trappole: --disable-tests non esiste, servono tre opzioni di percorso,
#    e senza le esclusioni di tar l'estrazione fallisce sempre.
bash qemu/scripts/build-qemu.sh

# 2. Innesta i nostri sorgenti e ricompila. Vuole git, quindi Git Bash.
bash qemu/scripts/innesta-winq.sh

# 3. Porta il binario nel runtime. Dichiara quale dei due impacchetta.
#    --bridge e --out sono obbligatori: il modo normale e' lasciar fare a
#    build-all.ps1, che li passa da se' (guest/scripts/build-all.ps1:151-152).
python guest/scripts/assemble_runtime.py --bridge <cartella-del-ponte> --out runtime
```

Se serve solo ricompilare dopo aver toccato un nostro file, e la patch è già applicata:

```bash
cp qemu/ui-winq/winq*.c qemu/ui-winq/winq*.h "$HOME/qemu-11.0.3/ui/"
MSYSTEM=CLANGARM64 /c/msys64/usr/bin/bash.exe -lc \
  "ninja -C $HOME/qemu-11.0.3/build qemu-system-aarch64.exe -j 10"
```

`ninja` incrementale su un solo file costa pochi secondi; da zero, 112 secondi con `-j 10`.

## Come si prova

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File "guest\scripts\avvia-android.ps1"
```

I default sono ora `-Eseguibile auto` (il nostro binario se c'è) e `-Display auto` (che dà `winq`). Lo script stampa cosa ha scelto. Per il confronto A/B:

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File "guest\scripts\avvia-android.ps1" `
  -Eseguibile runtime/bin/qemu-system-aarch64.exe -Display sdl
```

### Verificare il tocco

```bash
adb connect 127.0.0.1:5555
adb -s 127.0.0.1:5555 shell 'nohup timeout 60 getevent -t > /data/local/tmp/ev.txt 2>&1 &'
# toccare la finestra: premere, trascinare, RILASCIARE. Poi ASPETTARE che
# getevent esca, e solo dopo leggere:
adb -s 127.0.0.1:5555 shell 'grep -c " 0001 014a 00000001" /data/local/tmp/ev.txt'
adb -s 127.0.0.1:5555 shell 'grep -c " 0001 014a 00000000" /data/local/tmp/ev.txt'
```

**Entrambi** i conteggi devono essere maggiori di zero. Contare solo le occorrenze di `BTN_TOUCH` non basta: `console_handle_touch_event` è asimmetrica e senza la nostra compensazione si vedrebbero solo gli `1`, il conteggio sembrerebbe un successo, e il difetto emergerebbe più tardi come un trascinamento che non finisce mai.

Senza dita, `qemu/scripts/inietta-tocco.ps1` inietta tocchi **sintetici** e si dichiara tale.

## Le trappole, ognuna col suo sintomo

Sono tutte misurate su questa macchina. Il sintomo è scritto perché è ciò che si vede prima di sapere la causa.

**`getevent` bufferizza.** Leggere il file mentre il processo gira conta meno di quanto c'è. Si legge **dopo** l'uscita. Sintomo: una registrazione piena letta come «zero eventi».

**I log vecchi ingannano.** `guest/logs/sessione-viva.log` e `qemu-stderr.log` restano dopo la VM. Sintomo: un `boot_completed` di due ore prima preso per la prova appena fatta, o un'asserzione vecchia riportata come nuova. Cancellarli prima di ogni prova, o guardarne l'mtime.

**Il dispositivo di puntamento deve accordarsi al backend.** `virtio-mouse` dichiara `MASK_BTN` ma non `MASK_MTT`; `virtio-multitouch` il contrario per i `REL`. QEMU **non avvisa** quando un evento non ha destinatario: `qemu_input_find_handler` (`ui/input.c:101-122`) lo scarta in silenzio. Sintomo con il dispositivo sbagliato: al guest arrivano i `BTN_TOUCH` ma **non le posizioni** — Android sa che un dito è appoggiato e non sa dove, quindi «il tocco non fa nulla» mentre ogni verifica sul codice risulta corretta. Misurato: 398 eventi consegnati a `virtio-mouse-device`, zero al multitouch che non c'era. Il lanciatore ora lo risolve da sé con `-Puntatore auto`.

**La tabella dei tasti è `atset1`, non `win32`.** In keycodemapdb «win32» sono i **codici virtuali** (VK), non gli scancode. Sintomo: premendo `M` arriva `2`, perché `0x32` è sia lo scancode di `M` sia `VK_2`. Un tasto plausibile e sbagliato, senza nessun errore.

**I tasti estesi usano il prefisso `0xe000`, non il bit `0x80`.** In `atset1` gli estesi sono `[0xe048] = UP`, `[0xe053] = DELETE`, e fra `0x80` e `0xff` la tabella ha **zero voci**. Sintomo con `0x80`: le frecce non arrivano affatto, in silenzio.

**Il touchpad di precisione genera tocchi fantasma.** `PT_TOUCHPAD` e `PT_MOUSE` passano per gli stessi `WM_POINTER*` del dito, con `POINTER_FLAG_INCONTACT` acceso. Sintomo: Android che si muove da solo, e uno scorrimento irregolare perché il fantasma litiga col dito vero. Il filtro `PT_TOUCH`/`PT_PEN` in `winq-window.c` li esclude — e per questo il mouse come dito passa da un percorso **separato** (`WM_LBUTTON*`), non da `EnableMouseInPointer`, che li farebbe rientrare.

**Una misura fatta da un processo non DPI-aware non è una misura.** Da quando `winq` dichiara `PER_MONITOR_AWARE_V2` la finestra riceve i pixel fisici, ma un processo esterno che **non** si dichiara aware continua a vedere lo spazio virtualizzato: misurato su questa macchina, `GetSystemMetrics` dice `1440x960` a un processo non-aware e `2880x1920` a uno per-monitor-v2, nello stesso istante. Uno script non-aware che leggesse `GetClientRect` o catturasse lo schermo misurerebbe quindi la propria stranezza e non il prodotto — sintomo storico: una cattura che mostra una «finestra nera» dove la finestra funziona. Per questo `inietta-tocco.ps1` e `verifica-dpi.ps1` dichiarano la consapevolezza **prima** di leggere qualunque geometria. `qemu/scripts/verifica-dpi.ps1` è il modo di leggere DPI e area cliente delle due finestre senza aprire il guest.

**`AdjustWindowRect` mente dentro un processo per-monitor-aware.** Calcola lo spessore del bordo col DPI di sistema rilevato all'**avvio del processo**, che in un processo per-monitor-v2 è un numero fermo mentre il bordo vero segue il monitor. Sostituita con `AdjustWindowRectExForDpi` e il DPI di `GetDpiForWindow`. Sintomo se si lascia: dopo una rotazione l'area cliente non corrisponde alla risoluzione chiesta, e ricompaiono bande nere o un ritaglio — visibile **solo** premendo «ruota», non all'avvio.

**La pompa messaggi va per prima.** L'ordine in `.dpy_refresh` è `winq_pump_messages` → `graphic_hw_update` → `winq_present_frame`, così gli eventi entrano nella coda virtio prima che il guest disegni. Prezzo misurato: un flag alzato adesso si vede al giro dopo, ~30 ms sul ridimensionamento.

**`WM_PAINT` va validato.** Senza `BeginPaint`/`EndPaint` il messaggio resta in coda e `PeekMessage` lo ripesca a ogni giro: la pompa gira a vuoto per sempre.

## Difetti aperti

**Il guest ottiene OpenGL ES 3.1: risolto.** Fino al 30/07 restava a ES 2.0, perche' virglrenderer chiede contesti GL **desktop** e ANGLE offre solo GLES, quindi ogni richiesta falliva con `EGL_BAD_MATCH` e virgl ripiegava sul proprio percorso. La soluzione e' un contesto host **WGL** (`OPENGL32.dll` -> OpenGLOn12 -> D3D12), che su questa macchina da' `4.6 (Core Profile)`. Ora e' il default; `WINQ_GL=angle` torna al percorso precedente. Costava caro: 34 ms di mediana contro 5, e 318 contro 535 di punteggio glmark2.

## UN CICLO MODALE DI WINDOWS FERMAVA LA MACCHINA VIRTUALE — RISOLTO IL

**Lo stato di adesso, in una riga: la finestra vive su un thread proprio, e nessun ciclo
modale puo' piu' fermare il ciclo principale di QEMU.**

| prova | prima | dopo |
|---|---|---|
| linea di base, nessun gesto | 250 campioni, zero buchi | 149 campioni, zero buchi sopra 300 ms, peggiore 246 ms |
| bordo tenuto | **un buco di 23.848 ms** | 367 campioni, due buchi (356 e 314 ms), peggiore **356 ms** |
| spostare la finestra | un buco di 11.029 ms | **osservato dall'utente**: non si blocca piu'. Senza flusso, quindi senza numero |

**I limiti di questa verifica, perche' i numeri sopra non sono gemelli.** Nella corsa col
bordo, la tenuta e' stata **breve** e non di 60 secondi: quella riga dice che non e'
comparso niente che assomigli a un blocco, non che un minuto di bordo tenuto sia stato
esercitato. I due buchi residui stanno nella banda del peggiore a riposo, quindi
somigliano al rumore del campionamento -- ma sono a ridosso della soglia, e chiamarli
rumore e' un'interpretazione. La prova piu' forte e' un'osservazione diretta senza numero:
spostare la finestra intera non ferma piu' il guest. Dettaglio in
`.superpowers/sdd/ft-task-4-report.md`, con l'elenco di cio' che non e' stato provato.

**Il codice, in due file.** `winq-window.c` tiene la finestra e il suo thread; il suo
`wndproc` non chiama **niente** di QEMU -- traduce ogni messaggio in un record e lo accoda
(`winq-coda.h`). `winq-ciclo.c` tiene tutto cio' che gira sul ciclo principale, e il
drenaggio e' l'unico posto in cui quei record diventano chiamate a QEMU.

**LA REGOLA DA NON ROMPERE, e chi tocca questi file la incontri qui: dal ciclo principale
alla finestra si POSTA, non si MANDA mai.** `SetWindowPos` consegna `WM_SIZE` in modo
**sincrono** al thread proprietario: chiamarla dal ciclo principale mentre quel thread e'
dentro un ciclo modale lo bloccherebbe ad aspettarlo, cioe' rifarebbe esattamente il
guasto qui sotto -- e lo rifarebbe sul percorso della **rotazione**, che con il
ridimensionamento non c'entra niente e che nessuno andrebbe a guardare. Da qui
`WINQ_MSG_DIMENSIONA`. Le letture non sono coinvolte: `GetClientRect`, `GetDpiForWindow` e
`GetDC` non passano dalla coda del proprietario e non bloccano.

**Misurato PRIMA di scrivere il codice** (`qemu/probe/sonda-thread.c`, eseguita con
`qemu/scripts/sonda-thread.sh`): `SwapBuffers` chiamata dal thread che possiede il
contesto GL **non** aspetta il thread proprietario della finestra mentre quello e' dentro
un ciclo modale -- peggiore 11,6 ms su 1772 campioni presi con quel thread fermo. Se
avesse aspettato, l'intero approccio sarebbe caduto, ed e' il motivo per cui quella sonda
e' stata scritta prima e non dopo.

---

### La misura originale, che resta perche' e' il motivo per cui la correzione conta

Questa e' stata la scoperta piu' grossa su questa finestra, e smentisce la spiegazione che
questo stesso documento dava del «ridimensionamento che lagga».

**La misura.** Un flusso `adb shell` stampa l'ora del GUEST ogni 200 ms. Se il ciclo
principale di QEMU si blocca, nessuno serve virtio e il flusso si interrompe: i buchi
fra due timestamp consecutivi sono il tempo in cui il guest **non e' stato servito**.

| gesto | esito |
|---|---|
| nessuno (linea di base, 50 s) | 250 campioni, **zero buchi**, tutti sotto 300 ms |
| trascinare il **bordo** tenendo premuto | **un buco di 23.848 ms** su 276 campioni |
| **spostare** la finestra dalla barra del titolo | **un buco di 11.029 ms** su 418 campioni |
| menu di sistema | **non attribuito**: due buchi sotto il secondo, nella stessa banda di altri sei che compaiono anche a riposo |

**La causa, letta nel codice e coerente con la misura.** La pompa dei messaggi vive
dentro `winq_tick`, che e' un timer del **ciclo principale di QEMU**. Quando l'utente
afferra un bordo o la barra del titolo, Windows entra in un ciclo modale dentro
`DefWindowProc` **sullo stesso thread**: finche' non si rilascia, il ciclo principale
non gira, il BQL non viene rilasciato e la VM e' ferma. Non c'e' nessun
`WM_ENTERSIZEMOVE` in `winq-window.c`, e non servirebbe (vedi sotto).

**Non e' un difetto della presentazione.** Il paragrafo qui sotto attribuiva il
«ridimensionamento non molto funzionante» ai 30 Hz del refresh e poi al chiavistello
della catena di scambio. Entrambe le cose erano vere e nessuna delle due era questa:
quelle spiegavano un'immagine che inseguiva, questa spiega un guest **fermo per
ventiquattro secondi**.

**Tre rimedi valutati, e due non tengono:**

- **`WM_ENTERSIZEMOVE` piu' un `SetTimer`** e' la ricetta comune, e qui non basta: un
  `WM_TIMER` dentro il ciclo modale permette di ridisegnare, ma **non** fa girare il
  ciclo principale di QEMU, quindi virtio resta non servito. Curerebbe l'immagine e
  non la misura di sopra.
- **Reimplementare spostamento e ridimensionamento a mano** (`WM_NCLBUTTONDOWN`,
  `SetCapture`, `SetWindowPos` nostro) evita il ciclo modale ed e' contenuto in un
  file, ma perde Aero Snap, il doppio clic per massimizzare e il ridimensionamento da
  tastiera: Windows li regala dentro quel ciclo.
- **La finestra su un thread proprio**, separata dal ciclo principale di QEMU, e' la
  causa radice: nessun ciclo modale, presente o futuro, potrebbe piu' bloccare la VM.
  Costa piu' delle altre due -- il contesto GL e' legato al thread, quindi va deciso
  chi disegna e chi pompa -- e vuole spec e piano.
  **E' la strada presa, e il contesto GL NON si e' mosso**: resta sul ciclo
  principale, e il thread della finestra si limita a pompare e tradurre. Vedi in cima a
  questa sezione.

**Ipotesi da non dare per buona, ma da non perdere:** il guest perde tempo reale a
blocchi di decine di secondi, e il Watchdog di Android uccide `system_server` quando un
servizio non risponde entro 60 s. Il difetto «`system_server` muore» potrebbe avere qui
una delle sue cause, e sarebbe la prima spiegazione che non richiede di incolpare
l'immagine.

**Come sta questa ipotesi dopo la correzione, al.** Diventa piu' facile da
smentire che da confermare, e la smentita arrivera' da se': se `system_server` continua a
morire adesso che i blocchi da decine di secondi non ci sono piu', questa causa e' esclusa
senza doverla inseguire. Un dubbio da tenere presente e **non verificato**: il Watchdog
uccide con una firma sua (`WATCHDOG KILLING SYSTEM PROCESS` piu' il dump «blocked in
handler on»), mentre le tre occorrenze registrate hanno tutte la firma opposta -- `FATAL
EXCEPTION IN SYSTEM PROCESS` da una `RuntimeException` binder non catturata. Se in quei
tre casi il buffer `crash` fosse stato letto per intero, sarebbero due modi di morire
diversi e l'ipotesi non spiegherebbe nessuno dei tre. Non risulta annotato da nessuna
parte se sia stato letto per intero, quindi resta un dubbio e non una conclusione.

**Il ridimensionamento e le raffiche di input: risolti dal battito dedicato.** Erano due sintomi della stessa causa. QEMU chiama `.dpy_refresh` dal proprio timer generico ogni `GUI_REFRESH_INTERVAL_DEFAULT` = 30 ms, quindi la coda Win32 si svuotava una volta ogni 30 ms e tutti i `WM_POINTERUPDATE` accumulati venivano consegnati a virtio **nello stesso istante**. Misurato con `getevent`, distanze fra `SYN_REPORT` consecutivi di un dito reale, prima:

```
+35.3  +38.4  +28.4  +22.4  +27.8 ms     il ciclo di refresh
+0.1   +0.0   +0.0   +0.0   +0.0  ms     raffiche nello stesso istante
```

Android calcola la velocità del dito dai tempi fra i campioni: con campioni a distanza zero la velocità è indeterminata, e senza velocità non c'è inerzia.

Rimedio: un timer dedicato a **5 ms** (`WINQ_PERIODO_MS` per cambiarlo, da 1 a 30) armato con `timer_new_ms(QEMU_CLOCK_REALTIME, ...)` sul **ciclo principale**, che pompa i messaggi e presenta. Nessun thread: la texture del guest vive nel contesto GL del thread principale e condividerlo è il primo rischio che la spec vieta. È lo stesso meccanismo con cui QEMU arma `gui_update` in `gui_setup_refresh` (`ui/console.c:108-124`).

Dopo, con un trascinamento sintetico di 40 passi: un solo `+0.1` iniziale, poi spaziature regolari, **40 passi → 40 `SYN_REPORT`**, 39 posizioni, nessuna fusione e nessuna perdita.

**Misura ancora mancante, dichiarata:** il ritmo di campionamento con un **dito vero**. L'iniettore sintetico produce a ~30 ms di suo, quindi non può mostrare se ora si catturi a 5 ms. Serve un trascinamento lungo e continuo col dito, letto con `getevent`: le distanze dovrebbero scendere verso gli 8,3 ms del digitalizzatore a 120 Hz.

**Il primo frame dopo il ridimensionamento, e un chiavistello che si spegneva per sempre.** Il limite di tentativi con cui la presentazione fa convergere la catena di scambio di ANGLE era un chiavistello di *processo* invece che di *ridimensionamento*: tre misure discordi in tutta la vita del processo lo spegnevano definitivamente, e da quel momento il frame finale di ogni trascinamento poteva restare di scala sbagliata. `WM_SIZE` ora lo azzera con `winq_present_nuova_dimensione()`. Era parte del «ridimensionamento non molto funzionante» riferito dall'utente, che questo documento attribuiva ai soli 30 Hz: quella descrizione era edulcorata.

**Il kernel del guest si inchioda a intermittenza** all'avvio, per una corsa nell'accensione delle vCPU secondarie sotto WHPX: seriale ferma a `[0.000000]` con QEMU vivo. Indipendente da questo backend. `avvia-android.ps1` lo rileva contando le righe di seriale a sedici secondi e riprova fino a tre volte.

## Limiti dichiarati della v1

- **Nessuna semantica da scrivania per il mouse**: niente clic destro né rotellina. Richiederebbe un secondo dispositivo di puntamento, e Android **fonde** due dispositivi virtio in un'unica voce logica perché non portano identificatori distinti — sintomo misurato: cursore che si muove e clic che cadono al centro. Il mouse è quindi un dito singolo.
- Nessuna rotazione dello schermo, nessun display multiplo.
- Nessun audio.
- Nessun guscio applicativo: il prodotto resta l'eseguibile QEMU con la nostra finestra.
- **`WM_DPICHANGED` è implementato, e non è provato — nemmeno in simulazione.** Non è una mancanza di un secondo monitor: `WM_DPICHANGED` porta il rettangolo suggerito come un **puntatore** in `lParam`, e il messaggio non è marshallato. Una `SendMessage` cross-processo — l'unico modo di "simularlo" dal di fuori, come farebbe uno script di verifica — farebbe dereferenziare a `winq` un indirizzo valido solo nello spazio di memoria del *mittente*: spazzatura, o un crash. Farlo per davvero costerebbe una di due cose: codice di prova scritto **dentro** il prodotto apposta per l'occasione, oppure `VirtualAllocEx` + `WriteProcessMemory` per piazzare il `RECT` nello spazio di `winq` da fuori — scrivere nella memoria di una QEMU **viva**, con `data.img` montato `cache=writeback`, per il solo scopo di provare un gestore di quattro righe. Nessuna delle due è stata pagata. Quello che resta vero: il gestore c'è (`winq_wndproc`, ramo `WM_DPICHANGED`, in `winq-window.c`), applica il rettangolo suggerito con `SetWindowPos` e si appoggia allo stesso percorso già verificato del ridimensionamento (`WM_SIZE`, che aggiorna `winq.geom` e riarma la catena di scambio di ANGLE). Il sintomo, se qualcosa non regge, sarà visibile subito al primo monitor con scala diversa collegato — finestra sfocata o della dimensione sbagliata — e non silenzioso.

## I file

| file | responsabilità | thread |
|---|---|---|
| `winq-window.c` | il thread della finestra, la finestra, il `wndproc`, la traduzione dei messaggi in record. **Non chiama niente di QEMU** | finestra |
| `winq-ciclo.c` | `DisplayChangeListener`, drenaggio della coda, battito, presentazione a cadenza, tubo del guscio, annuncio della frequenza, risoluzione | principale |
| `winq-coda.c` | la coda fra i due thread. Non include QEMU, e `qemu/scripts/test-coda.sh` lo compila da solo per renderlo verificabile | entrambi |
| `winq-present.c` | contesto GL (`DisplayGLCtxOps`), presentazione della texture di scanout e del percorso 2D | principale |
| `winq-wgl.c` | il contesto host OpenGL desktop | principale |
| `winq-input.c` | traduzione del tocco e della tastiera, compensazione di `BTN_TOUCH` | principale |
| `winq-coord.c` | mappatura finestra→guest, funzione pura, con 27 casi di prova | principale |
| `winq.h` | stato condiviso, e le **due** funzioni che attraversano il confine fra i due file. Se quella lista si allunga, il confine si sta sfilacciando |  |

`winq-coord.c` non include nulla di QEMU né di Windows: è l'unica parte testabile in isolamento e va tenuta tale. I test si eseguono con `bash qemu/scripts/test-coord.sh`, in MSYS2 CLANGARM64.

## Riferimenti

- `qemu/QEMU-BUILD.md` — compilare QEMU dai sorgenti, con le sue trappole

