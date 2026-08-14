# guscio: Habumi.exe

Il supervisore Win32 che avvia QEMU con Android dentro, mostra i suoi progressi, e lo spegne pulito comunque lo si chieda -- da un bottone, da una finestra, o da Windows stesso.

Sorgenti in `app/guscio/`, compilati in `runtime/bin/Habumi.exe` da `app/scripts/build-guscio.sh`.

## Cos'e' e cosa non e'

E' un supervisore, non un'interfaccia: apre QEMU, lo sorveglia con una macchina a stati (`vm.c`), e mostra in una finestra propria cio' che sta succedendo (`finestra.c`) -- le fasi dell'avvio col loro tempo, i bottoni hardware che Android non ha, il registro (`registro.c`) dove finiscono le tre sorgenti che contano: le decisioni del guscio, lo stderr di QEMU, la seriale del guest.

**Non tocca la finestra di Android.** Quella e' `-display winq` (`qemu/ui-winq/`, vedi `qemu/HOST-WINDOW.md`): un'altra finestra Win32, quella dove il guest disegna e dove arrivano tocco e tastiera. Il guscio non ne conosce il contenuto, solo il ciclo di vita del processo che la possiede -- e un evento con nome (`AndroidRuntimeGuscio.chiusura`) che quella finestra segnala quando l'utente la chiude.

**Non e' un installer.** Si spedisce una cartella (`runtime/bin/`): l'eseguibile, le DLL di QEMU, `adb.exe` accanto, `config.txt`. Nessun pacchetto, nessuna voce di registro, nessun collegamento creato da solo.

**L'audio c'e'.** Il kernel del guest ha ora virtio-snd builtin, quindi `/dev/snd` esiste e l'HAL apre uscita **e** ingresso; `audio=on` e' il default. Cio' che manca non e' l'audio ma qualcosa che suoni: vedi la sezione dell'audio piu' sotto, e il limite sulla memoria esterna che non si monta.

## Come si compila e si prova

Toolchain: `clang -std=c11 -Wall -Werror`, sempre dentro MSYS2 CLANGARM64 -- in questo sottosistema il compilatore e' clang, non gcc -- e sempre con percorsi assoluti dentro le virgolette, perche' quella shell parte dalla propria home e non dalla radice del progetto.

```bash
# compila runtime/bin/Habumi.exe
MSYSTEM=CLANGARM64 /c/msys64/usr/bin/bash.exe -lc "bash <radice>/app/scripts/build-guscio.sh"

# le prove senza VM: registro, config, vm, dpi, adb, apk, file, rilascio, archivio,
# appunti-trama, varianti, dati
MSYSTEM=CLANGARM64 /c/msys64/usr/bin/bash.exe -lc "bash <radice>/app/scripts/test-guscio.sh"
```

Esito atteso della seconda riga (**556** prove in tutto, al ):
```
test-registro: 30 su 30 passati
test-config: 60 su 60 passati
test-vm: 77 su 77 passati
test-dpi: 30 su 30 passati
test-adb: 33 su 33 passati
test-apk: 16 su 16 passati
test-file: tabella ANSI di questa macchina = 1252
test-file: 22 su 22 passati
test-rilascio: percorsi 2000 file in 0.5 ms
test-rilascio: 72 su 72 passati
test-archivio: 36 su 36 passati
test-appunti-trama: 50 su 50 passati
test-varianti: 95 su 95 passati
test-dati: 35 su 35 passati
```

Il totale in testa e' la **somma di quei numeri**, non un numero a se': due volte in due giorni
qui c'era scritto un totale che non tornava piu' (prima 186, poi 286 contro 291 reali). Chi
aggiorna una suite aggiorni anche la somma, o la tolga.

**E' successo una QUINTA volta.** Questo blocco era rimasto fermo a 399 anche dopo
che i piani `mt-` (mappatura dei tasti) e `sv-` (selettore di varianti) avevano cambiato quasi
tutti i numeri elencati -- `test-vm` da 55 a 77, `test-config` da 55 a 60, e due suite intere
nuove, `test-varianti` e `test-dati` -- senza che nessuno tornasse a sommare. Corretto il
, ricontando con lo stesso comando invece che a mente: **556**.

**Le dipendenze dichiarate in `test-guscio.sh` sono la documentazione ESEGUIBILE dei confini.**
Ogni prova linka solo i moduli che il suo modulo ha diritto di usare: se `rilascio.c` cominciasse
a chiamare `vm_stato()`, la sua prova **non compilerebbe**. E' cosi' che «`rilascio.c` non dipende
da `vm.c`» resta vero invece di restare scritto in un commento.

Due righe non sono conteggi e non vanno lette come tali. `test-file` **stampa la
tabella ANSI** perché le sue prove sui nomi accentati la assumono 1252: su una macchina
con un'altra tabella quel numero spiega un fallimento che altrimenti sembrerebbe un difetto
del codice. `test-rilascio` **stampa il tempo** della percorrenza di duemila file invece di
asserire una soglia, perché una prova che asserisce un tempo lampeggia sulla macchina di
qualcun altro.

I conteggi di `test-adb` e `test-apk` sono cambiati senza che si sia perso
niente: tredici casi si sono spostati dall'uno all'altro insieme alla funzione
`apk_ultima_riga`, diventata `adb_ultima_riga`. La somma era 40 prima e resta 40 dopo. E il
`53` che stava scritto qui per `test-vm` era **disallineato da prima**: sono 55.

**E' successo una terza volta**, trovato aggiungendo `test-appunti-trama`: qui
c'era scritto `test-dpi: 22` mentre la suite ne stampa **30**, e il totale `327` valeva quindi
per una riga che nessuno aveva piu' eseguito leggendo. Corretti tutti e due: 30 per `test-dpi`,
e **385** di somma. **E una QUARTA volta lo stesso giorno**, aggiungendo la chiave
`porta_appunti`: `test-config` e' passata da 41 a 55 prove e la somma da 385 a **399**,
mentre la riga in testa diceva ancora 385. La lezione e' sempre la stessa e ormai ha
quattro occorrenze: questi numeri si
ricopiano da un'esecuzione vera, non si aggiornano a mente sommando il proprio contributo.

Nessuna di queste prove avvia QEMU: costerebbe due minuti ciascuna. Il ciclo di vita vero (`vm_apri`/`vm_passo`/`vm_avvia_spegnimento`) si verifica avviando `runtime/bin/Habumi.exe` dalla radice del progetto e guardando la finestra e `guest/logs/sessione-viva.log`.

Due modi senza VM, per verificare la configurazione senza pagare l'avvio:
```
runtime\bin\Habumi.exe --config       (* stampa i default, la configurazione risolta, e cosa e' stato ignorato *)
runtime\bin\Habumi.exe --argomenti    (* stampa la riga di comando che darebbe a QEMU, ed esce *)
```

## Le chiavi di configurazione

`runtime/bin/config.txt`. Non serve per avviare: le chiavi assenti prendono il default, e un valore illeggibile non ferma nulla -- viene ignorato e la ragione finisce nel registro (`config_carica` in `app/guscio/config.c`, mai una riga generica "valore non valido").

| chiave | default | intervallo | note |
|---|---|---|---|
| `vcpu` | 6 | 1..64 | |
| `memoria` | 6144 | 1024..65536 (MB) | |
| `larghezza` | 1280 | 320..8192 (px) | |
| `altezza` | 800 | 320..8192 (px) | |
| `gl` | wgl | wgl / angle | wgl da' ES 3.1 via D3D12, angle ripiega su ES 2.0 |
| `riprove` | 3 | 1..10 | tentativi prima di dichiarare il kernel inchiodato |
| `porta_adb` | 15555 | 1024..65535, **mai 5554..5585** | vedi sotto |
| `audio` | **on** | on / off | uscita e ingresso via virtio-snd; vedi la sezione dell'audio |
| `scala_guest` | no (spedito: si) | si / no | moltiplica la risoluzione del guest per la scala dello schermo. **Costo misurato**: vedi sotto |
| `hz` | 0 | 0 (automatico) oppure 24..240 | frequenza annunciata al guest via `WINQ_HZ`; spedita a 120. Vedi sotto |
| `densita` | 0 | 0 (non tocca) oppure 120..640 | `wm density <valore>` applicato una volta appena Android e' pronto; spedita a 426. Vedi sotto |

**Perche' non 5554-5585 per `porta_adb`.** adb esplora quell'intervallo in coppie (console sulla pari, protocollo adb sulla dispari) per scoprire da solo gli emulatori. Con l'inoltro dentro quell'intervallo adb scambia la nostra porta per un emulatore, si inventa un'entrata `emulator-5554`, tenta la stretta di mano della console su quella porta, non trova nulla, e lascia OFFLINE sia quell'entrata inventata sia la nostra -- con adbd che nel guest gira regolarmente. Il guasto e' intermittente perche' dipende dai tempi: ha funzionato per ore e poi ha smesso senza che nulla, sul lato guest, fosse cambiato. `config_carica` rifiuta un `porta_adb` in quell'intervallo e scrive la ragione nel registro invece di accettarlo in silenzio.

**Perche' `audio=on` di default,.** Prima era spento per una ragione buona: il kernel aveva `CONFIG_SND=m` senza moduli installati, quindi `/dev/snd` non esisteva e la chiave avrebbe acceso un dispositivo che nessuno apriva. Ora i quattro simboli (`SOUND`, `SND`, `SND_PCM`, `SND_VIRTIO`) sono builtin, i nodi ci sono, e l'HAL apre entrambe le direzioni: la chiave e' diventata una funzione. Un emulatore muto sorprende chi apre un video, quindi il default e' acceso; spegnendola spariscono sia `-audiodev` sia il device.

**Perché `scala_guest=si` è il default spedito, e cosa costa davvero.** Con `no` il guest disegna la risoluzione configurata e la finestra riceve i pixel veri dello schermo: da due scalature in fila si passa a una, e si guadagna nitidezza a costo zero. Con `si` la risoluzione chiesta al guest viene moltiplicata per la scala, e su questa macchina (200%) il guest diventa **2560x1600** — **quattro volte** i pixel di 1280x800. Il costo non è più ignoto, è **misurato**: a 2560x1600@60 la mediana GPU passa da 4 a 7 ms, il frame totale da 13 a 24 ms, i frame scattosi dall'1,14% all'8,47% (legacy: dal 14,86% al 52,54%), la CPU di SurfaceFlinger da 570 a 980 ms, i frame persi dalla GPU da 89 a 458. Un aumento che porterebbe un lato oltre 8192 viene rifiutato **intero**, con la ragione nel registro: limitare un solo lato cambierebbe le proporzioni del guest.

**Perché `hz` e `densita` esistono, e perché sono spediti a 120 e 426.** Alzare la risoluzione con `scala_guest` porta due conseguenze che queste due chiavi correggono. La prima è la frequenza: a 2560x1600@120 i frame scattosi salgono ulteriormente, dall'8,47% al 34,40% (legacy: dal 52,54% al 94,80%), perché il budget per frame scende da 16,7 a 8,3 ms mentre la sola mediana GPU è già 9 ms. **60 sarebbe difendibile** — divide esattamente sia 60 sia 120, restando senza scatti in entrambi gli stati del refresh variabile del pannello — ma **120 è la scelta fatta sapendo questi numeri**, non una svista. La seconda è la densità: `wm density` viene dall'EDID, che QEMU genera dalla geometria, e a 2560x1600 con la densità di un 1280x800 (213) l'interfaccia diventa minuscola — misurato, la colonna del feed occupava un terzo dello schermo. `densita=426` (213 raddoppiato come la risoluzione) corregge questo, applicato una volta da `main.c` (ramo `VM_PRONTO`) con `wm density`, che scrive in `/data` e quindi persiste finché `data.img` non cambia — verificato riavviando la VM.

## Le cinque vie d'uscita

Tutte misurate su questa macchina, ognuna con l'ultima riga di seriale che la conferma.

| via | come si esce | esito misurato | ultima riga di seriale |
|---|---|---|---|
| chiudere la finestra del guscio | `WM_CLOSE` su `AndroidRuntimeGuscio` | spegnimento pulito di Android, poi QEMU esce da se' | `reboot: Power down` |
| chiudere la finestra di Android, guscio attivo | `WM_CLOSE` su `winq`, relayato al guscio via evento con nome | QEMU esce da se' in **4485 ms** | `reboot: Power down` |
| il guscio muore | due casi, opposti (vedi sotto) | vedi sotto | vedi sotto |
| Windows si spegne | `WM_QUERYENDSESSION` alla finestra del guscio | vedi sotto | vedi sotto |
| **il menu di spegnimento di Android** | `KEY_POWER` tenuto premuto, consegnato attraverso la finestra `winq` | Android si spegne da se', QEMU esce, `data.img` si rimonta **senza recupero ext4** | `reboot: Power down` |

### La quinta via: il tasto di accensione, ed e' l'unica che non passa da adb

**Quando serve.** Le prime quattro finiscono tutte, prima o poi, dentro `adb_spegni` --
cioe' `adb shell reboot -p`. Se `adbd` non risponde, quella catena non parte: il guscio
segnala, nessuno raccoglie, e dopo 30 s la guardia armata da `winq` esce **duro**. Con
`data.img` montato `cache=writeback` un'uscita dura equivale a staccare la corrente, che
e' esattamente cio' che il ramo `WM_CLOSE` esiste per evitare.

Il caso non e' teorico ed e' capitato : spegnendo le opzioni sviluppatore nel
guest -- cosa che un utente fa per far girare un'app con controllo d'integrita' -- si
spegne anche `adbd`, e riaccenderle **non basta**, perche' adbd riparte in modalita' USB
mentre questo canale e' TCP sulla 5555 inoltrata. Da fuori non si rimedia: serve un
riavvio di adbd, che si chiederebbe con adb.

**Come si fa.** Si posta un tasto alla finestra di Android, che lo traduce in un evento
virtio-keyboard come qualunque altro:

```
scancode atset1 0x5e con il bit degli ESTESI (lParam bit 24) = 0xe05e = POWER
PostMessage(hwnd_winq, WM_KEYDOWN, 0x5F, 0x015E0001)
  ... tenere ~1,5 s, o Android lo legge come pressione breve e spegne solo lo schermo ...
PostMessage(hwnd_winq, WM_KEYUP,   0x5F, 0xD15E0001)
```

L'HWND si trova con `EnumWindows` sulla classe `winq`, **non** con `FindWindowA` (vedi
"Le trappole"). Si **posta** e non si manda, per la stessa ragione di
`WINQ_MSG_DIMENSIONA`: una `SendMessage` da un altro processo bloccherebbe il chiamante
finche' il thread della finestra non la serve.

**Cosa succede, misurato :** compare il menu di spegnimento di Android.
Toccando «Spegni», Android chiude le proprie partizioni da se' -- ed e' **piu' pulito di
`reboot -p`**, perche' passa dalla sequenza di spegnimento completa del framework invece
che da un comando esterno. Verificato dopo il riavvio successivo: `reboot: Power down`
nella seriale, e nessuna riga di errore ext4 o di recupero del journal al rimontaggio di
`data.img`.

**Cosa NON e' questa via.** Non e' una funzione del prodotto e non e' automatica: apre un
menu, e qualcuno deve toccare «Spegni». E' una via di servizio, da usare quando adb non
c'e'. Il tasto passa dalla tabella `atset1` gia' esistente: non e' stato aggiunto niente
al codice per ottenerla, e infatti e' stata trovata per necessita', non progettata.

**Chiudere la finestra del guscio.** `fin_wndproc` intercetta `WM_CLOSE`, non lo lascia scendere a `DefWindowProc`: si rimanda a `main.c` (`WM_APP+1`), che chiama `vm_avvia_spegnimento()` se Android e' pronto o in avvio. `adb_spegni` manda `reboot -p`, Android si spegne, QEMU esce da se', il guscio distrugge la propria finestra solo allora (`VM_USCITO`). Se QEMU e' gia' uscito da se' (`VM_MORTA`) o l'avvio e' fallito (`VM_FALLITA`), si chiude senza aspettare adb: nulla da spegnere.

**Chiudere la finestra di Android col guscio attivo.** La finestra `winq` non chiude piu' QEMU direttamente (`qmp_quit`): segnala l'evento con nome `AndroidRuntimeGuscio.chiusura` e ARMA da sola una scadenza di 30 s, nel caso il guscio non risponda. Il guscio lo vede al giro successivo del proprio timer (ogni 500 ms) e prende la stessa via dello spegnimento pulito. Misurato: **4485 ms** dal `WM_CLOSE` a QEMU uscito, ultima riga `reboot: Power down`.

**Il guscio muore, due casi opposti.**
- *Ucciso DOPO che la chiusura e' stata segnalata* (l'utente chiude la finestra di Android, il guscio muore prima di finire lo spegnimento): l'evento e' gia' stato letto, ma il guscio non c'e' piu' per portare a termine `adb_spegni`/attendere QEMU. La scadenza di 30 s armata da `winq` stesso scatta: la guardia in QEMU esce da sola. Misurato: **31,24 s** dal `WM_CLOSE`, nessuno zombie.
- *Ucciso PRIMA che l'utente chiuda* la finestra di Android: l'evento con nome muore insieme al processo del guscio (nessun altro ne tiene una maniglia aperta). Quando l'utente chiude poi la finestra, `winq_segnala_chiusura` prova ad aprire un evento che non esiste piu', fallisce, e ricade sul vecchio comportamento -- `qmp_quit` diretto. Misurato: uscita DURA in **1,15 s**. Non peggio di prima del guscio, ma non protetto: vedi "Limiti dichiarati".

*Per confronto, senza guscio* (`guest/scripts/avvia-android.ps1`, che chiama `qmp_quit` direttamente su `WM_CLOSE`, come sempre): uscita dura in **1,51 s**. E' il ramo che il lanciatore vecchio esercita sempre, e per questo resta nel repository (vedi la nota in testa al file).

**Windows si spegne.** Sezione a se', perche' e' l'ultima via aggiunta e la piu' delicata da provare: vedi sotto.

### Windows si spegne: come si e' verificato, e cosa NON e' stato provato

Non si e' spento Windows per davvero: avrebbe chiuso la sessione di chi ha eseguito questo lavoro, con tutto cio' che ci girava sopra. Si e' invece mandato a mano il messaggio che Windows manda quando comincia a spegnersi -- `WM_QUERYENDSESSION` (0x0011) -- alla finestra del guscio, trovata via `EnumWindows` (non `FindWindowA`: vedi "Le trappole"), con `SendMessage`.

`fin_wndproc` risponde a `WM_QUERYENDSESSION` esattamente come a un `WM_CLOSE` chiesto dall'utente: rimanda `WM_APP+1` e ritorna `TRUE`. Rifiutare (ritornare `FALSE`) bloccherebbe lo spegnimento del computer, che nessuno vuole per via di un emulatore rimasto aperto. `WM_ENDSESSION` (il messaggio che segue, quando Windows ha davvero deciso di procedere) si limita a scrivere una riga nel registro: a quel punto non c'e' piu' tempo per aspettare uno spegnimento pulito.

Risultato letterale della verifica:

```
finestra del guscio trovata: hwnd=1311816
invio WM_QUERYENDSESSION
risposta a WM_QUERYENDSESSION: 1 (atteso: 1/TRUE)
riga 'reboot: Power down' trovata dopo 4067,59 ms dall'invio
nessun processo Habumi/qemu-nostro vivo dopo 4086,08 ms dall'invio
```

La finestra ha risposto `TRUE` (non ha rifiutato la chiusura), ha innescato lo stesso spegnimento pulito delle altre vie d'uscita, e sia la riga finale di seriale sia l'uscita di entrambi i processi sono arrivate a circa **4,07 s** dall'invio -- coerente con i 4485 ms misurati in quella verifica per lo stesso percorso interno (`vm_avvia_spegnimento` -> `adb_spegni` -> `reboot -p`), che `WM_QUERYENDSESSION` si limita a richiamare tramite lo stesso `WM_APP+1` di un `WM_CLOSE`.

**Cosa questa verifica NON copre, dichiarato invece di nascosto.** Mandare `WM_QUERYENDSESSION` a mano esercita davvero la logica del gestore -- la risposta `TRUE`, l'innesco dello spegnimento pulito, `WM_ENDSESSION` che registrerebbe la propria riga se arrivasse -- ma NON riproduce il vincolo di tempo reale di uno spegnimento vero di Windows. Quando Windows spegne il computer per davvero, concede alle applicazioni solo pochi secondi fra `WM_QUERYENDSESSION` e l'uccisione forzata dei processi (il valore esatto non e' documentato in modo affidabile e varia con la versione di Windows e le policy di gruppo). Lo spegnimento pulito misurato in questa sessione costa **circa 4 secondi** (4067-4086 ms), coerente con i 4485 ms di quella verifica. Ci sta di misura, ma non sempre: se Windows concedesse meno di quanto misurato qui -- ed e' un margine stretto, non ampio -- i processi verrebbero uccisi a meta' dello spegnimento di Android, con lo stesso esito pratico di "il guscio muore ucciso DOPO il segnale" descritto sopra (nessuno zombie, ma nessuna garanzia che `reboot -p` abbia scritto la partizione dati in modo pulito). Questo margine non e' stato e non poteva essere misurato in questa sessione senza spegnere davvero la macchina, cosa che ci era esplicitamente vietato: chiuderebbe la sessione di chi ha eseguito questo lavoro.

## Quale meccanismo di rotazione e' servito

**Non basta lo scambio delle dimensioni.** Il tubo con nome (`\\.\pipe\AndroidRuntimeGuscio.comandi`, `tubo.c`) manda a `winq` la risoluzione scambiata (`larghezza`/`altezza` invertite), e `winq` la applica davvero al device virtio-gpu (`dpy_set_ui_info` accetta, il device notifica `VIRTIO_GPU_EVENT_DISPLAY` al guest). Misurato: dopo il comando, `wm size`, `mRotation`, l'orientamento (`dumpsys | grep -E "land|port"`) e uno screencap restano ESATTAMENTE quelli di prima, non solo "coricati" -- il guest non si e' riconfigurato affatto in risposta all'evento.

**E' servito anche `user_rotation` via adb.** Il ripiego che effettivamente ruota lo schermo: `adb shell settings put system accelerometer_rotation 0` (altrimenti Android ripristina da solo la propria scelta) seguito da `adb shell settings put system user_rotation <0 o 1>`. Il verso si decide leggendo la stessa chiave che si sta per scrivere e invertendola -- non da "wm size", che in questo QEMU non cambia mai, e che quindi sceglierebbe sempre lo stesso verso e non tornerebbe mai indietro (misurato: con quella logica, due pressioni consecutive hanno dato `user_rotation=1` entrambe le volte).

Misurato dopo la correzione: `user_rotation` alterna **0 -> 1 -> 0 -> 1 -> 0** a pressioni successive del bottone "ruota", e uno screencap dopo ogni cambio conferma l'orientamento corretto (verticale/orizzontale, non solo il numero nelle impostazioni).

### Una rotazione ha ucciso system_server, una volta sola

**Osservato dall'utente, catturato, NON riprodotto.** Alla prima rotazione dopo
un avvio la schermata d'avvio e' ricomparsa; le rotazioni successive sono andate
bene. Non era QEMU: nel registro c'e' un solo `tentativo 1 di 3` e il tempo del
guest **non riparte da zero**. Si e' riavviato lo spazio utente di Android.

Il momento esatto, 840 ms dopo il cambio di risoluzione:

```
[1604281 ms] rotazione: verso il verticale, framebuffer chiesto 1600x2560
[1604281 ms] winq: risoluzione del guest -> 1600x2560 a 120 Hz
[1605125 ms] binder_alloc: 434: binder_alloc_buf, no vma
[1605125 ms] binder: cannot allocate buffer: vma cleared, target dead or dying
[1605125 ms] init: starting service 'bootanim'
```

e la causa vera, dal buffer `crash` del logcat:

```
FATAL EXCEPTION IN SYSTEM PROCESS: android.display
java.lang.RuntimeException: Unknown error
    at android.os.BinderProxy.transactNative(Native Method)
    at android.hardware.display.IDisplayManagerCallback$Stub$Proxy.onDisplayEvent
    at DisplayManagerService$CallbackRecord.notifyDisplayEventAsync
    at DisplayManagerService.deliverDisplayEvent
```

system_server consegna l'evento di schermo cambiato a un client registrato, la
transazione binder fallisce, e sul thread `android.display` del processo di
sistema questo e' **fatale**. Le righe del kernel vengono DOPO: quando
SurfaceFlinger prova a parlargli, la mappatura binder di system_server e' gia'
sparita perche' sta morendo. Tutto il resto -- systemui, media, i nostri appunti
-- muore con `DeadSystemException`, che e' conseguenza e non causa.

**Tentativi di riproduzione, tutti falliti:**

| prova | esito |
|---|---|
| `wm size 1600x2560` da adb su avvio fresco | sopravvive: il ridimensionamento LOGICO non basta, serve il cambio di modo vero |
| bottone "ruota" pilotato con `PostMessage(WM_COMMAND, ID_RUOTA)` dopo un ciclo di `wm size` | sopravvive |
| stesso bottone come PRIMA azione dopo un avvio fresco | sopravvive |
| ritorno all'orizzontale | sopravvive, `user_rotation` 1 -> 0 |
| bottone premuto **a mano dall'utente** su avvio fresco | sopravvive: quindi "prima pressione dopo l'avvio" NON e' il grilletto |
| sette app aperte prima di ruotare, client di schermo saliti da **6 a 13** | sopravvive: nemmeno il numero di client lo spiega |

Sei rotazioni su tre avvii e due configurazioni diverse, nessun crash. Il caso
originale aveva in piu' **26 minuti di attivita'**, e non e' stato possibile
ridurlo a una condizione piu' piccola di cosi'. Il sospetto resta un client di
`IDisplayManagerCallback` finito in cattivo stato -- non la rotazione in se', che
e' solo il primo evento che prova a raggiungerlo -- ma **e' un sospetto, non una
diagnosi**: nessuna delle prove qui sopra lo conferma.
Il bottone si preme da codice cosi', senza mani:

```powershell
Add-Type -Namespace W -Name N -MemberDefinition '[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);'
[W.N]::PostMessage((Get-Process -Name Habumi).MainWindowHandle, 0x0111, [IntPtr]1007, [IntPtr]0)
```

**Punto aperto**: perche' il guest ignori `VIRTIO_GPU_EVENT_DISPLAY` non e' stato investigato. Il device lato host fa la propria parte (verificato leggendo `hw/display/virtio-gpu-base.c` nell'albero QEMU locale), ma questa immagine e questo kernel non reagiscono al cambio annunciato in nessuna forma misurabile. Resta un ripiego via adb, non una correzione della causa.

**Il bottone "ruota" chiedeva sempre il verticale, e il difetto era invisibile a chi guarda il video. CORRETTO.** Il gestore `ID_RUOTA` ricavava il bersaglio da `adb_risoluzione_guest` (cioe' `wm size`), che in questo QEMU non cambia MAI, e lo mandava SCAMBIATO: `tubo_risoluzione(h, w)`. Quindi ogni pressione chiedeva la stessa cosa. MISURATO nel registro del prodotto prima della correzione -- quattro pressioni, quattro volte `1600x2560`, mentre il verso nella riga di registro alternava correttamente:

```
rotazione: verso il verticale   (framebuffer 2560x1600 -> 1600x2560)
rotazione: verso il orizzontale (framebuffer 2560x1600 -> 1600x2560)   <- chiede il verticale!
```

Sintomo per chi guarda: **lo schermo si vede ruotare comunque**, perche' `user_rotation` (l'altra meta' del meccanismo) alterna davvero -- ma il framebuffer restava verticale per sempre, e quando Android tornava orizzontale il contenuto veniva scalato dentro le bande. Per questo il difetto era stato descritto come "non fa niente dalla seconda pressione", che era falso: fa qualcosa, e sbagliato.

Il bersaglio ora si ricava dal **verso** e non dallo scambio (`dpi_ruota` in `dpi.c`, provata in unita' anche sul caso che distingue le due logiche: una geometria di partenza GIA' verticale deve dare lo stesso bersaglio). MISURATO sul prodotto dopo la correzione: `1600x2560 -> 2560x1600 -> 1600x2560 -> 2560x1600`, e le righe `winq: risoluzione del guest ->` di QEMU mostrano l'alternanza ricevuta. Alla quarta pressione Android e' orizzontale (`user_rotation=0`, `mBounds 2560x1600`, `land`) e l'ultimo framebuffer chiesto e' `2560x1600`: **combaciano**, cosa che prima non succedeva mai.

**Resta aperto, ed e' il pezzo successivo:** la finestra di Android e' `1280x800` pixel fissi (`winq_create_window(1280, 800)` in `qemu/ui-winq/winq-window.c`), quindi un framebuffer verticale viene comunque schiacciato in un client orizzontale. La rotazione e' corretta nel guest e nel framebuffer, non nella forma della finestra.

Nota storica: il giro `1280x800 -> 800x1280 -> 1280x800` usato al tempo per verificare la geometria del DPI era stato ottenuto scrivendo DIRETTAMENTE sul tubo dei comandi, non col bottone -- quella verifica resta valida, ma passava per una strada diversa da quella dell'utente, ed e' il motivo per cui questo difetto e' sopravvissuto a due revisioni.

## Trascinare un APK sulla finestra

Si rilascia un `.apk` sulla finestra del guscio, si conferma, e viene installato in Android.

| dettaglio | com'e', e perche' |
|---|---|
| cosa accetta | **solo** file che finiscono per `.apk`, maiuscole indifferenti. Un file qualunque produce una riga nel registro e **nessun dialogo**: copiare file nel guest e' un'altra funzionalita', non un errore dell'utente |
| la conferma | `MB_OKCANCEL` con il predefinito su **Annulla** (`MB_DEFBUTTON2`): un `Invio` premuto per distrazione non deve installare niente |
| quanti file | fino a `RIL_MAX_VOCI` (16) per rilascio. Oltre, il rilascio si rifiuta **intero**, invece di installarne alcuni e tacere sugli altri. (Si chiamava `APK_MAX_FILE` fino al : il limite e' del rilascio, non degli APK) |
| come installa | `adb install -r`, cioe' reinstallando e **conservando i dati**: e' quello che serve a chi ricompila la stessa app venti volte |
| dove gira | su un **thread**, non nel `wndproc`: `adb install` dura secondi, e nel wndproc congelerebbe anche il pannello del registro, cioe' proprio la cosa che deve raccontare l'avanzamento |
| quante alla volta | **una**, con `InterlockedCompareExchange` e non un `bool`: fra un «se e' libero» e un «lo prendo» ci sta un secondo trascinamento. Due `adb install` paralleli sullo stesso dispositivo non danno un errore chiaro, danno un errore confuso |
| cosa si legge | l'**ultima riga non vuota** di adb, cioe' `Success` oppure `Failure [...]`. Si guarda l'uscita e non il codice di ritorno, perche' adb esce 0 anche quando fallisce |

**Cosa e' provato e cosa no.** Provate: le tre decisioni pure (29 prove in `test-apk.c`); che la
finestra accetti file (`WS_EX_ACCEPTFILES` letto dall'esterno sulla finestra vera: `exstyle=0x110`);
e che il comando costruito funzioni davvero — `adb install -r "<percorso>"` su un APK estratto dal
guest ha risposto `Performing Streamed Install` / `Success`, e `adb_ultima_riga` (allora
`apk_ultima_riga`) su quell'uscita da'
esattamente `Success`.

**Non provato: il gesto.** Un `WM_DROPFILES` sintetico da un altro processo non e' fattibile in
modo sicuro — l'`HDROP` e' una maniglia valida solo nello spazio del processo che l'ha creata,
esattamente come il `RECT` di `WM_DPICHANGED` (vedi piu' sotto). Serve un trascinamento vero con il
mouse. Il percorso e' verificato in tutti i pezzi tranne quel gesto.

## L'archivio delle prove

Due file del guscio nascono puliti a ogni avvio, e per due ragioni buone: la seriale
(`guest/logs/sessione-viva.log`, cancellata in `vm_apri`) perche' un log vecchio farebbe contare
righe di un avvio precedente e dichiarare sano un kernel inchiodato; il registro
(`guest/logs/registro-guscio.log`, aperto con `"w"`) perche' un registro che si accumula fa
prendere per nuovo un messaggio del penultimo avvio.

**Insieme rendevano impossibile indagare un guasto raro.** `system_server` e' morto
durante un'installazione: la riga del kernel che nominava il pid giusto e' esistita, ed e' andata
perduta al riavvio successivo. Di quel crash restano solo le righe copiate a mano prima di
riavviare.

Ora `archivio.c` **sposta** il file di prima invece di distruggerlo:

```
guest/logs/archivio/sessione-viva-20260804-151016.log
guest/logs/archivio/registro-guscio-20260804-151015.log
```

| dettaglio | com'e', e perche' |
|---|---|
| quanti se ne tengono | `ARCH_QUANTI` = 10 per ciascuno dei due. Il difetto che ha reso necessario questo modulo si e' presentato **tre volte in quattro giorni**: dieci avvii coprono la finestra in cui e' ragionevole aspettarsi la prossima occorrenza |
| il nome | `AAAAMMGG-hhmmss` con gli zeri davanti, perche' l'ordine **alfabetico** deve essere quello cronologico: la potatura cancella i primi, e senza gli zeri «9» verrebbe dopo «10» e si butterebbe il file sbagliato |
| due avvii nello stesso secondo | il secondo prende un suffisso `-2`. Il guscio riprova l'avvio fino a tre volte quando il kernel si inchioda, e `MOVEFILE_REPLACE_EXISTING` distruggerebbe proprio la prova che stiamo conservando |
| se non si riesce ad archiviare | il file attivo viene **cancellato comunque**, e l'avvio prosegue. Un file che sopravvive all'avvio e' la trappola del «`boot_completed` di due ore prima»: perdere una prova e' meno grave che leggere dati vecchi credendoli nuovi |
| per tornare indietro | `ARCH_QUANTI` a 0 ripristina il comportamento di prima (cancella, nessun archivio) senza togliere il codice |
| dove NON si registra l'esito | dentro `registro_apri`, perche' il registro non e' ancora aperto e non puo' lamentarsi di se stesso. Per la seriale invece la riga c'e' |

**Verificato sul prodotto, non dedotto:** avviato due volte, i due file del giro precedente sono
negli archivi con **MD5 identico** all'originale (`13be7401…` per la seriale, `21aa4d61…` per il
registro) — spostati, non ricreati vuoti — e i file attivi sono nati puliti. La potatura a dieci e'
provata in unita' con i nomi attesi, non sul prodotto: servirebbero undici avvii.

## Trascinare un file o una cartella sulla finestra

Si rilascia un file qualunque sulla finestra del guscio, si conferma, e finisce in
`/sdcard/Download` dentro Android. Fino al 03/08 quel gesto produceva una riga di registro e
niente altro: solo i `.apk` venivano serviti.

| dettaglio | com'e', e perche' |
|---|---|
| dove finisce | `/sdcard/Download`, **fisso**. E' la cartella che le app aprono per prima; nessuna chiave in `config.txt` finche' non serve |
| cartelle | **accettate**, ricorsive. Il dialogo dichiara quanti file e quanti byte, perche' per dirlo bisogna averli contati -- e il conteggio avviene prima del dialogo, cioe' dentro il `wndproc` |
| quante voci | fino a `RIL_MAX_VOCI` (16) per rilascio, e fino a `RIL_MAX_ALBERO` (2000) file dentro le cartelle. Oltre, il rilascio si rifiuta **intero** |
| rilascio misto (APK + file) | **un solo dialogo** che dichiara le due azioni, poi `install` prima e `push` dopo. Un'app installata senza i suoi dati e' uno stato che si capisce; dei dati senza l'app no |
| la sostituzione | il file di prima **viene rimosso**, e il dialogo lo dice con queste parole -- non «sostituito», perché una sostituzione *fallita* non lascia né il nuovo né il vecchio (misurato, vedi le trappole). Controllare le collisioni prima costerebbe un `adb` dentro il percorso della UI, che bloccherebbe la finestra fino alla propria scadenza |
| cartelle che non si contano | se un albero è più profondo di 32 livelli, o una sottocartella non si legge, il rilascio si rifiuta **intero** invece di copiarne una parte: `adb push` è ricorsivo per conto suo e **non** conosce i nostri limiti, quindi copierebbe più di quanto il dialogo ha dichiarato e su un conteggio piccolo la scadenza sarebbe troppo corta |
| nomi non ANSI | la voce si **salta**, con una riga che dice *quale* voce e non come si chiama -- stampare il nome vorrebbe dire convertirlo, cioe' fare la conversione che stiamo rifiutando. Gli accentati passano (tabella 1252) |
| quante alla volta | **una**, con un lucchetto `Interlocked` **unico** per install e push: due adb in parallelo sullo stesso dispositivo non danno un errore chiaro, ne danno uno confuso |
| se Android non e' pronto | rifiutato **prima** di lanciare qualsiasi adb, dicendo di attendere la fase «pronto». Prima di questo, trascinare durante l'avvio dava `device not found` |
| la scadenza | commisurata al lavoro: **20 s + byte / 10 MB/s + 40 ms per file**, e il registro scrive quanti secondi sono stati concessi |
| MediaStore | **nessun comando**. Un file copiato compare da se' entro 4 s, perche' `/sdcard` e' montata da MediaProvider via FUSE |

### La scadenza, e perche' non e' quella che sarebbe stata meglio

La cosa giusta sarebbe una scadenza di **inattivita'**, che distingue «lento» da
«piantato». Non e' implementabile, ed e' misurato: **adb non stampa il progresso su un tubo
rediretto**. Con 600 MB in trasferimento, `stdout` e `stderr` sono rimasti a zero byte per
sette secondi e poi hanno ricevuto 127 byte alla fine; e `adb help` offre `-q` per
**sopprimere** il progresso, nessun flag per pretenderlo. Il segnale non esiste.

Quindi il tempo si commisura al lavoro, e i due termini vengono da due misure:

| termine | misura che lo motiva |
|---|---|
| `byte / 10 MB/s` | 115 MB/s su 200 MB a cache calda, 84 su 600 MB, 57 a freddo, **27,9 su 1,2 GB attraverso il prodotto**. L'andatura peggiora col crescere del file, quindi il pavimento sta 2,8 volte sotto il **peggiore**, non sotto la media |
| `40 ms per file` | 500 file piccoli costano 3,44 s, cioe' ~6,9 ms l'uno, mentre i loro byte valgono 4 ms. Coi soli byte una cartella al tetto avrebbe avuto i 20 s di sola base contro i ~14 che le servono: funzionava qui e si rompeva sulla prima macchina piu' lenta |

Numeri e protocollo in `app/misure/pf-misure-preliminari.md`.

### Cosa e' provato sul prodotto vivo, e con quali numeri

Tutto col dito dell'utente, :

| prova | esito |
|---|---|
| un file, e **Annulla** | `rilascio: annullato`, niente copiato |
| un file, e OK | `1 file pushed (44 bytes)`, «20 s concessi» |
| una cartella con 5 file su due livelli | `5 files pushed (1575 bytes)`, e nel guest ci sono tutti, sottocartella compresa |
| un nome cirillico | `la voce 1 ha un nome che questo canale non trasporta, salto`, e **nessun dialogo** |
| APK **piu'** un file, insieme | un solo dialogo, poi `Success` dell'install **prima** della copia |
| **1,2 GB** | «140 s concessi», `1 file pushed. 27.9 MB/s (1258291200 bytes in 43.004s)`, e **MD5 identico** fra host e guest (`d85e2deb…`). 43 secondi contro i 20 della scadenza di prima: e' la prova che la scadenza commisurata serve |
| **il testo del dialogo, letto sullo schermo** | una cartella da **1024 byte esatti** ha dato «2 file (**1,0 KB**)», non «1024 byte»: il confine fra le unita' regge nel prodotto e non solo in `test-rilascio`. E l'avviso letto e' quello nuovo (il file di prima **viene rimosso**), non la vecchia promessa di sostituzione |

### Le trappole di questa funzionalita'

**Il centro della finestra non e' un punto di rilascio.** Il pannello del registro copre
quasi tutta l'area cliente, e un controllo figlio non ha un drop target proprio: chi prova a
guidare un trascinamento da uno script deve scegliere un punto nei margini. Ci si sbaglia una
volta e sembra che il prodotto non accetti i file.

**Una sostituzione fallita non lascia ne' il nuovo ne' il vecchio.** Misurato: un file era
stato copiato (44 byte, verificato), il secondo tentativo e' fallito con `remote couldn't
create file`, e nel guest non c'era **piu' niente**. Il dialogo promette che i file con lo
stesso nome verranno sostituiti; se la sostituzione fallisce, quello di prima e' comunque
perduto. Non e' aggirabile dal nostro lato: adb apre il file remoto prima di sapere se
riuscira'.

**Il canale adb puo' cadere mentre la VM e' viva.** Se `system_server` muore -- difetto noto
e aperto -- Android si riprende ma la connessione adb resta chiusa, e il guardiano
`vm_stato() == VM_PRONTO` **non se ne accorge**, perche' quello e' lo stato della VM e non
quello del canale. Per questo il thread del rilascio chiama `adb_pronto` una volta per
rilascio: riconnette da se'.

### Cosa di questa funzionalita' NON e' provato, elencato invece di taciuto

- **Il gesto** del trascinamento: vedi i limiti dichiarati in fondo al documento. Provato
  tutto il resto del percorso.
- **Il rifiuto durante l'avvio** e' provato in unita' su tutti gli stati non pronti, non col
  gesto: la finestra utile dura trentacinque secondi e il rilascio a mano e' arrivato dopo
  tre minuti.
- **Il rifiuto del secondo rilascio mentre il primo e' in corso.** Il lucchetto esiste, e'
  unico, ed e' preso con `InterlockedCompareExchange`; la riga di registro c'e'. Ma il caso
  **non e' stato esercitato** ne' a mano ne' in unita' (in unita' servirebbe un rilascio
  vero in corso). Il codice e' quello che serviva gia' agli APK.
- **Un trasferimento in corso mentre il guscio chiude.** Non provato, e con una ragione per
  cui conta: `registro_riga` controlla `reg_aperto` e *poi* entra nella sezione critica,
  mentre `registro_chiudi` la distrugge. E' una corsa **preesistente** (rilievo minore 2 del
  registro dei piani), ma il thread del rilascio puo' stare in quella finestra per **minuti**
  invece dei venti secondi di un comando corto: la probabilita' e' cresciuta, il difetto no.
- **Cosa faccia `adb push` con un reparse point.** Noi lo saltiamo (una giunzione verso un
  antenato farebbe girare la ricorsione per sempre) e lo scriviamo nel registro. Se adb lo
  seguisse, copierebbe piu' di quanto il dialogo ha dichiarato. Non misurato.
- **Che un mp3 vero venga tipizzato** nella collezione audio di MediaStore: provato solo
  l'ingresso in `MediaStore.Files`, con una fixture di byte casuali.
- **Quanto costi `dexopt`** in un'installazione: la scadenza di `install` riusa la formula
  misurata su `push`, e il tempo di un'installazione non sta nei byte dell'APK.

## Appunti condivisi con Windows

Si copia su Windows e si incolla in Android, e viceversa, **automaticamente**: nessun
bottone, nessun gesto in piu'. Solo testo.

MISURATO sul prodotto vivo, nei due versi:

```
appunti: 22 byte mandati al guest
appunti: 8 byte dal guest negli appunti di Windows
```

e dal lato guest, letto con un APK di prova che stampa cio' che vede:
`APPUNTI: voce[0]=<<GODZIDROID-ANDATA-9182>>`, cioe' esattamente la stringa copiata
sull'host.

### I tre cancelli pagati PRIMA di progettare, e cosa hanno cambiato

| domanda | esito | conseguenza |
|---|---|---|
| gli appunti di Android si pilotano da `adb shell`? | **NO** | serve un'app dentro il guest |
| basta che l'app sia di sistema? | **NO**: `READ_CLIPBOARD_IN_BACKGROUND` e' `prot=signature\|role`, letto da `dumpsys package` | serve la firma di piattaforma |
| l'immagine e' firmata con la testkey pubblica di AOSP? | **SI'**: certificato di `framework-res.apk` identico byte per byte a `platform.x509.pem`, SHA-256 `c8a2e9bc...51192ab8` | si puo' firmare, e il permesso arriva |

`cmd clipboard` esiste come servizio ma **non ha implementazione da shell**; il binder
grezzo si raggiunge ma la firma della transazione va indovinata e cambia fra versioni di
Android, e marshallare un `ClipData` a mano per la SCRITTURA non e' praticabile. Senza il
terzo cancello questa funzione avrebbe richiesto una activity trasparente che lampeggia a
ogni copia.

### Com'e' fatta

| pezzo | dove |
|---|---|
| `guest/appunti-guest/` | l'app Android, firmata di piattaforma, in `/system/app/` |
| `app/guscio/appunti-trama.{c,h}` | modulo PURO: trama, guardia contro l'eco, tetto. 50 prove, si compila da solo |
| `app/guscio/appunti.c` | server su `127.0.0.1`, `AddClipboardFormatListener` sulla finestra del guscio |
| `guest/scripts/android-initramfs-init.sh` | l'overlay che mette l'APK in `/system/app` |

**Il canale e' TCP verso `10.0.2.2`**, cioe' l'host visto dal guest attraverso la SLIRP che
gia' c'e'. Il guest chiama, l'host ascolta: cosi' il guscio non deve sapere quando la VM
esiste, e un riavvio del guest si ripara da se'. Scartate: `virtio-serial`, che sarebbe
piu' canonico ma costerebbe una regola su `ueventd.rc` dentro l'immagine; e **adb**, che
avrebbe lo strato gia' pronto ma **muore quando si spengono le opzioni sviluppatore** --
cioe' proprio quando l'emulatore serve, visto che i giochi con controllo d'integrita' non
partono con quelle accese.

**Trama: 4 byte big-endian di lunghezza piu' UTF-8.** Non righe terminate da newline, ed
e' il motivo per cui NON si riusa il tubo dei comandi: quello ha l'accumulo a 512 byte e
il terminatore `\n`, quindi non trasporta testo lungo ne' multiriga.

**LA GUARDIA CONTRO L'ECO e' la cosa che fa funzionare tutto.** L'host scrive nel guest,
il listener del guest scatta, rimanda all'host, l'host riscrive: l'anello e' il modo in
cui questa funzione si rompe. Ciascun lato ricorda **l'ultimo valore ricevuto dall'altro**
-- il valore, non un flag ne' un contatore -- e non lo rispedisce. Nel guest il valore si
registra PRIMA di `setPrimaryClip`, perche' il listener puo' scattare dentro quella
chiamata. La prova che regge: dopo una sessione di prove il registro contava **tre**
trasferimenti, dove un anello ne farebbe centinaia.

### I due difetti trovati sul prodotto, ed erano invisibili senza il logcat

**1. Mancava `RECEIVE_BOOT_COMPLETED`.** Dichiarare il receiver nel manifesto non basta:
Android rifiuta la consegna e lo scrive **una volta sola**.

```
BroadcastQueue: Permission Denial: receiving Intent
{ act=android.intent.action.BOOT_COMPLETED } to com.godziller.habumi.clipboard/.AvvioReceiver
requires android.permission.RECEIVE_BOOT_COMPLETED
```

Il sintomo era «gli appunti non si sincronizzano», che somiglia a un difetto del canale.
E' un permesso NORMALE: la firma di piattaforma serve per LEGGERE gli appunti, non per
ricevere il boot.

**2. Android fermava il servizio dopo un minuto di inattivita'.**

```
ActivityManager: Stopping service due to app idle: u0a152 -1m19s711ms
com.godziller.habumi.clipboard/.ServizioAppunti
```

La connessione cadeva di conseguenza, e il sintomo sarebbe stato «funziona per un minuto e
poi basta». Curato con `android:persistent`, che Android onora **solo per le app in
`/system`** -- dove l'overlay dell'initramfs la mette gia'. L'alternativa era un servizio
in primo piano, che pretende una notifica permanente nella barra di stato del guest.
Costo dichiarato: un processo sempre vivo, piccolo ma non gratis.

### Una trappola di metodo, che e' costata mezz'ora

`service call clipboard 1` rispondeva **`No items` anche con gli appunti pieni**. E' uno
strumento di lettura che non era mai stato validato contro uno stato noto, e per un po' ha
fatto credere che l'app non scrivesse. Chi deve leggere gli appunti del guest usi un APK
che stampa cio' che vede, non quel comando.

### Limiti dichiarati

- **Solo testo.** Niente immagini, niente file, niente formati ricchi.
- **Tetto di 1 MiB**, oltre il quale si **rifiuta** invece di troncare: appunti troncati in
  silenzio sono peggio di appunti non passati.
- **La porta e' compilata dentro l'APK.** `porta_appunti` in `config.txt` cambia
  solo il lato host: cambiarla scollega il guest, e il guest **non se ne lamenta** perche'
  l'assenza del guscio e' un caso normale. Per questo il guscio confronta con la costante
  e scrive nel registro che l'APK va ricostruito con `guest/appunti-guest/build.sh`.
- **Se il guscio non c'e', il testo copiato nel guest si PERDE** invece di partire alla
  riconnessione. E' voluto: consegnarlo dopo sovrascriverebbe gli appunti dell'host con
  qualcosa copiato dieci minuti prima, e degli appunti conta solo l'ultimo stato.
- **Una sola istanza dell'emulatore**: una porta sola, nessun caso previsto per due.
- **ESPOSIZIONE:** l'ascoltatore sta su `127.0.0.1`, quindi lo raggiunge il guest ma anche
  qualunque altro processo di questa macchina, e negli appunti ogni tanto ci sono
  password. Su una macchina a utente singolo e' modesta ma reale. Si chiuderebbe con un
  token condiviso, che pero' andrebbe consegnato al guest, e con adb spento non e' banale.
- Il consumo a riposo della connessione persistente **non e' misurato**.

## GodziDroid: nome, animazione e lingua, senza toccare le immagini

Tutta la personalizzazione viaggia nell'**initramfs**, che passa da 1,12 a 1,61 MB. Ne
segue la cosa che conta per la release: **`system.img` e `vendor.img` non si modificano**,
quindi possono essere scaricate vergini dall'utente e non redistribuite da noi -- che era
il primo vincolo dichiarato di qualunque piano di rilascio.

> **QUESTA FRASE ERA FALSA QUANDO E' STATA SCRITTA, ed e' diventata vera solo
>.** `vendor.img` conteneva l'HAL audio con l'anello da 4096 frame, installato il
> 02/08 e mai spostato. Se ne e' accorto solo chi, tre giorni dopo, e' andato a preparare la
> release e ha confrontato l'immagine con la sua riserva: md5 diversi. Nessuna prova poteva
> accorgersene, perche' nessuna prova guarda le immagini.
>
> Corretto spostando quel file nell'overlay -- vedi la sezione «L'HAL audio fuori
> dall'immagine» piu' sotto. Da allora la frase e' vera per entrambe le immagini.
>
> La lezione, che vale oltre questo caso: **una frase che dichiara un invariante va
> verificata quando la si scrive, non quando serve.** Questa e' stata creduta per tre
> giorni ed e' finita a fare da premessa a un piano di rilascio.

| cosa | dove finisce | come |
|---|---|---|
| APK degli appunti | `/system/app/AndroidRuntimeAppunti/` | overlay di sola lettura su `/system` |
| animazione d'avvio | `/system/product/media/bootanimation.zip` | stesso overlay |
| lingua `en-US` | `/system/build.prop` | stesso overlay, con `sed` |
| nome **GodziDroid** | `/odm/etc/build.prop` | l'overlay del **vendor**, che gia' esisteva |

**L'overlay su `/system` e' possibile perche' `/system` NON e' la radice**: l'immagine e'
un rootfs vero in cui `/system` e' una sottodirectory (inode 52), quindi ci si monta sopra
esattamente come l'overlay del vendor si monta su `/android/vendor`, prima dello stesso
`switch_root`. E' di **sola lettura**, a due `lowerdir`: nessun upperdir, nessun workdir,
nessun copy-up possibile.

**Il nome non passa da `/system`**, e il log lo dimostra:
`init: Setting product property ro.product.model to '...' (from ro.product.odm.model)`.
Per le `ro.product.*` vince odm, e `/odm/etc` sta dentro `vendor.img`.

**Trappola, e ci si cade in silenzio:** le proprieta' gia' presenti vanno **sostituite**,
non accodate. Il codice accodava, e funzionava solo perche' quelle proprieta' non
c'erano; per le `ro.*` vince la PRIMA assegnazione, e `ro.product.odm.model` e
`ro.product.locale` ci sono gia'. Un append non farebbe niente e nessuno se ne
accorgerebbe. Da qui la funzione `imposta_prop`, che sostituisce se la riga c'e' e accoda
solo se manca.

**Debito dichiarato: SELinux.** L'initramfs non fa `restorecon` e non usa `context=`,
quindi i file dell'overlay prendono l'etichetta `tmpfs` invece di quella giusta. Passa
solo perche' la riga di comando ha `androidboot.selinux=permissive`, e si vede gia' oggi
nei log del vendor (`avc: denied ... permissive=1`). Il giorno in cui si passa a
enforcing, va sistemato.

**Verificato su `data.img` nuovo:** `ro.product.locale=en-US` e
`persist.sys.locale` **vuota**. Era l'unico dubbio rimasto sulla lingua: una
`persist.sys.locale` salvata in `data.img` da un avvio precedente vincerebbe sulla
`ro.*`, e su un `/data` vecchio puo' ancora succedere.

L'animazione e' un **segnaposto** generato a codice (`guest/bootanimation/genera.py`), non
un disegno. Due trappole di quel formato, entrambe verificate sull'artefatto: lo zip deve
essere **STORED** e non compresso, o Android mostra uno schermo nero **senza nessun
errore**; e `desc.txt` va per primo, con fine riga UNIX.

## Il passaggio all'immagine VANILLA

Da qui in poi il progetto gira su **LineageOS VANILLA**, cioe' senza GAPPS, e
**non modifica piu' nessuna immagine**: l'ultimo file che obbligava a scrivere
dentro `system.img` era `asound.conf`, e ora arriva dall'overlay come l'APK degli
appunti, l'animazione e le proprieta'. Ne segue la cosa che serviva alla release:
`system.img` e `vendor.img` si scaricano **vergini** da monte, e il progetto non
redistribuisce niente che non sia suo.

Provato sul prodotto vivo dopo lo scambio:

| cosa | esito |
|---|---|
| avvio | `sys.boot_completed=1`, zygote partito **una volta sola** |
| GAPPS | **0** pacchetti fra `com.google.android.*` e `com.android.vending` |
| nome | `ro.product.model` e `ro.product.name` = `GodziDroid` |
| lingua | `ro.product.locale=en-US`, `persist.sys.locale` vuota |
| launcher | fuoco su `com.android.launcher3/.uioverrides.QuickstepLauncher` |
| schermo | 2560x1600, densita' 426 |
| overlay | `asound.conf` 932 B, `bootanimation.zip` 653.591 B, l'APK degli appunti: tutti e tre in `/system` |
| audio | `/proc/asound/cards` -> `0 [SoundCard]: virtio-snd`; AudioFlinger con uscita mixer 48000 Hz, 2 canali, PCM_16_BIT |
| appunti | connessione **Established** sulla 15556 e `appunti: 23 byte mandati al guest`, che e' la lunghezza esatta della stringa copiata sull'host |

**L'audio si SENTE**, confermato a orecchio -- ma non subito: la
prova ha scoperto due guasti in fila, raccontati qui sotto. **Non provati a dito
su questa immagine**: tocco e rotazione, che erano stati provati sulla GAPPS.

### Il guest era muto, e nessuno dei due guasti era l'audio

Nessuna suoneria, nessuna notifica, nessun file audio. **Il sintomo porta fuori
strada**: il PCM ALSA resta `closed`, quindi sembra l'HAL o `asound.conf`, dove
non c'e' niente da trovare. Il PCM non si apre perche' non arriva mai un
campione: la catena si rompe molto prima dell'hardware.

Ed era invisibile perche' **l'audio dei giochi funzionava lo stesso**: i giochi
decodificano per conto loro e consegnano PCM, e i suoni di sistema erano spenti
(`sound_effects_enabled=0`). Niente aveva mai esercitato un decodificatore.

**Primo: `debug.stagefright.ccodec=0`, e l'avevamo messa noi.** In
`guest/scripts/android-vendor.props`, senza una riga di motivazione -- unica in
un file dove ogni proprieta' ha la sua. Copiata da `waydroid.prop`, non scelta.
A 0 spegne Codec2, e su Android 13 Codec2 e' **l'unico** percorso rimasto perche'
OMX e' stato rimosso. La lista dei codec risultava vuota, audio e video insieme:

```
$ dumpsys media.player
Decoder infos by media types:
=============================
                                  <- niente
```

Tolta invece di rimessa a un numero, cosi' vale il valore di piattaforma.

**Secondo: `/dev/dma_heap/system` a 0600, e ueventd.rc dell'immagine lo vuole
0444.** QUARTA volta che questo progetto incontra lo stesso schema, dopo
binderfs, `/dev/dri` e `/dev/input`: devtmpfs crea il nodo 0600, ueventd lo trova
gia' esistente e ne corregge **solo il gruppo**. `media.swcodec` gira come utente
`mediacodec`, non `system`, quindi non apre l'heap e Codec2 non alloca:

```
E MediaCodec: Codec reported err 0xfffffff4/NO_MEMORY ... state 5/STARTING
E NuPlayerDecoder: Failed to start [c2.android.vorbis.decoder] decoder (err=-12)
```

cioe' **"manca memoria" con 6 GB liberi**, e il decodificatore che si CREA e non
PARTE. Si applica `0444`, il modo scritto nell'immagine, non un numero scelto da
noi.

La prova che regge, sullo stesso tocco di suoneria:

```
prima:  closed closed closed closed closed closed
adesso: closed closed RUNNING RUNNING RUNNING RUNNING RUNNING RUNNING
```

**Una strada sbagliata, e vale la pena saperlo.** Vedendo che `/vendor` dichiara
tutto l'audio come `OMX.google.*` mentre i video sono gia' `c2.android.*`, la
prima correzione tentata e' stata riscrivere quei nomi nell'overlay. E' stata
costruita, provata e **rimossa**: quei nomi erano un effetto di `ccodec=0`, non
la causa. Con Codec2 riacceso la lista si popola da sola, alias OMX compresi.

### LA TRAPPOLA: cambiare `system.img` obbliga a rifare `data.img`

Il `/data` scritto dall'immagine GAPPS, sotto la VANILLA, manda il sistema in
ciclo. **E nessun messaggio nomina `/data`**:

```
[   20.735522] init: processing action (sys.boot_completed=1) ...
[   21.657030] binder: release 432:499 transaction 25400 in, still active
[   21.734236] init: Service 'zygote' (pid 287) received signal 9
```

L'avvio arriva **fino in fondo**, poi system_server (pid 432) muore un secondo
dopo e riparte per sempre: **zygote 90 volte**, e con lui netd, media,
cameraserver e audioserver, che sono suoi `onrestart`. Cio' che si vede nel log
sono 198 `failed to set task profiles`, i `/dev/stune/... No such file or
directory` e un `service vendor.audio-hal-4-0-msd not found`, che sono **rumore
presente anche negli avvii sani**: seguirli porta a diagnosticare l'audio. Con un
`/data` nuovo zygote parte una volta sola.

Non c'e' nessuno script che crei `data.img`: l'initramfs fa un `mount` secco
(`mount -o rw /dev/vdc /android/data`) e si ferma se non riesce. Si rifa' dalla
WSL, la stessa che costruisce l'initramfs:

```bash
truncate -s 4294967296 data.img
mke2fs -q -t ext4 -b 4096 -N 262144 -L data data.img
```

**Per la release e' un buco aperto**: chi installa scarica le immagini vergini, ma
`data.img` deve nascere da qualche parte, e oggi nasce a mano.

### Due modi di perdere tempo, pagati entrambi

**Il guscio vuole la cartella corrente sulla radice del progetto**, perche' dentro
usa percorsi relativi (`runtime\bin\qemu-nostro.exe`). Lanciato da altrove la
finestra si apre lo stesso e la porta 15556 pure, ma QEMU non parte e il registro
resta **vuoto**: le poche righe gia' scritte restano nel buffer perche' nessun
`[qemu]` arriva a farlo scaricare. Sembra un blocco, e' una cartella sbagliata.

**`debugfs` legge male un `/data` non smontato pulito.** Dice `Block bitmap
checksum does not match bitmap`, che si legge come corruzione; il superblocco pero'
ha `needs_recovery` e `orphan_present`, cioe' e' **journal da riapplicare**, e il
guest lo ripara da solo al mount. Non e' un difetto da inseguire.

## La rotella del mouse: arriva in Android, e non basta

`WM_MOUSEWHEEL` sulla finestra di Android viene convertito in scatti interi (con accumulo del
resto, perche' i touchpad di precisione mandano delta minori di 120) e spedito a QEMU come
`INPUT_BUTTON_WHEEL_UP/DOWN` **con mittente `NULL`**. La riga di comando aggiunge un
`-device virtio-mouse-pci` non legato ad alcuna console, e lega tastiera e multitouch con
`display=gpu0`: e' cosi' che la rotella finisce sul mouse e il tocco resta sul multitouch.

**Misurato sul prodotto vivo, e funziona fin quasi in fondo:**

| controllo | esito |
|---|---|
| il mouse esiste nel guest | `Classes: CURSOR \| EXTERNAL_STYLUS`, `/dev/input/event2` con `REL=103` (REL_X, REL_Y, REL_WHEEL) |
| la rotella arriva | `getevent` su `event2`: `0002 0008 ffffffff` = `EV_REL`/`REL_WHEEL`/-1, uno per scatto |
| l'instradamento e' giusto | su `event1` (il multitouch) **niente**: la rotella non gli arriva, quindi non ruba nulla |
| il tocco non e' regredito | tocco sintetico iniettato: down, 3 update, up, launcher a fuoco |
| Android la interpreta | genera `MotionEvent(source=MOUSE, action=SCROLL)` |
| **la lista scorre** | **no** |

**Perche' no, e non e' un'ipotesi:** quell'evento porta `xCursorPosition=0.0, yCursorPosition=0.0`.
Android manda lo scorrimento alla finestra **sotto il puntatore**, e il puntatore di quel
dispositivo e' nell'angolo in alto a sinistra perche' non gli abbiamo mai mandato un movimento.
Provato anche a spostarlo dall'esterno: `sendevent` non puo' (il nodo e' `root:input` e adb shell
non ci scrive), e `input mouse motionevent MOVE 640 500` sposta un puntatore **iniettato** ma non
quello del dispositivo — l'evento `SCROLL` successivo porta ancora `0.0, 0.0`.

**Quindi:** per rendere utile la rotella bisogna mandare anche il movimento (`REL_X`/`REL_Y`) dallo
stesso dispositivo, cioe' avere un puntatore vero in Android.

### DECISIONE PRESA : niente puntatore, e la rotella resta ferma dov'e'

L'utente ha scelto **no** al puntatore, sapendo che questo lascia la rotella incapace di scorrere, e
ha scelto di **non rimuovere** il codice. Quindi:

- **non riproporre il puntatore** e non «sistemare» la rotella facendola muovere: e' una scelta di
  prodotto, non una dimenticanza;
- **non rimuovere** `winq_rotella`, il `case WM_MOUSEWHEEL` o il `-device virtio-mouse-pci`
  pensando che siano codice morto lasciato per sbaglio;
- costo accettato consapevolmente: nella VM c'e' un dispositivo mouse in piu', che Android enumera
  come `CURSOR`, e qualche app puo' comportarsi diversamente credendosi su una macchina con mouse.
  Se un giorno si vedesse un'app comportarsi in modo strano, questo e' un sospetto da controllare.

Idea non verificata, annotata perche' e' la sola via che darebbe la rotella **senza** un cursore
fermo sullo schermo: in Android il cursore del mouse svanisce da se' dopo un periodo di inattivita'
(`PointerController`), quindi mandare il movimento **solo** nell'istante della rotella potrebbe far
comparire il cursore mentre si scorre e farlo sparire subito dopo. Da verificare prima di crederci.

## L'audio, acceso di default

`audio=on` e' il default. La riga di comando prende `-audiodev dsound,id=a0` e
`-device virtio-sound-pci,audiodev=a0`; spegnendo la chiave spariscono entrambi.

**Cosa mancava, e cosa e' cambiato.** Solo il kernel. Il guest non aveva `/dev/snd` perche'
`CONFIG_SOUND`, `CONFIG_SND` e `CONFIG_SND_PCM` erano a modulo (e non installiamo moduli) e
`CONFIG_SND_VIRTIO` non c'era affatto. Ora sono tutti e quattro **builtin**
(`guest/scripts/build-guest-kernel.sh`, con i simboli anche nella lista di verifica che gira prima
di compilare). Il resto della catena c'era gia' e non e' stato toccato: **nessuna modifica a
`vendor.img`**, nessuna proprieta' nuova, nessun HAL scritto da noi.

**Misurato sul prodotto vivo:**

| controllo | esito |
|---|---|
| nodi ALSA | `/dev/snd/pcmC0D0p` (uscita) e `/dev/snd/pcmC0D0c` (**cattura**), `system:audio` |
| scheda | `virtio-snd - VirtIO SoundCard at pci/0000:00:06.0/virtio5` |
| AudioFlinger | `Output thread AudioOut_D`, 48000 Hz, `PCM_16_BIT`, stereo |
| HAL, uscita | `adev_open_output_stream selects channels=2 rate=48000 format=2` |
| HAL, ingresso | `adev_open_input_stream selects channels=2 rate=16000 format=10` |
| errori dell'HAL | nessun `cannot open pcm_out driver`, nessun `Failed to open pcm_in` |
| regressioni dal kernel nuovo | nessuna: 2560x1600, densita' 426, launcher, tocco e mouse |
| avvio | 30 s contro i 26 di riferimento. **Non indagato**, e non si spaccia per uguale |

Android sceglie `audio.primary.waydroid.so` **da se'**, perche' `ro.hardware=waydroid` e
`hw_get_module` prova `audio.primary.<ro.hardware>` prima di `default` — e `default` e' lo stub da
11 KB che accetta l'audio e lo butta.

**L'AUDIO FUNZIONA, e la catena e' stata misurata pezzo per pezzo.**

Nella stessa corsa, con il tono a 440 Hz generato nel browser del guest:

| misura | valore |
|---|---|
| picco del dispositivo di uscita di Windows | **0,027** — diverso da zero: il suono arriva all'host |
| bussate del guest sulla coda TX | 55 in dieci secondi |
| buffer restituiti al guest da QEMU | **149** |
| chiamate della callback di uscita | 72 |
| `hw_ptr` del substream ALSA nel guest | **24320**, cioe' avanza |

Nessuno stallo: il flusso e' continuo, e la latenza dichiarata cicla regolarmente
3072 -> 2048 -> 1024 -> 0 a ogni giro di buffer.

**CORREZIONE DI UNA CONCLUSIONE SBAGLIATA, scritta qui poche ore prima.** Avevo scritto che
`virtio-snd` di QEMU consuma due buffer e si ferma, sulla base di una traccia che mostrava due soli
`tx queue callback` e di tre misure a picco zero. Era **falso**, e va detto perche' la ragione e'
istruttiva: in quelle corse **il tono non stava suonando**. Il tocco sintetico sul bottone della
pagina non registrava, quindi il guest apriva il PCM (da cui `state: RUNNING`, che mi aveva
convinto) ma non ci scriveva dentro nulla. Il picco a zero e il file `wav` vuoto erano la risposta
corretta a una domanda mal posta: non c'era niente da sentire. `state: RUNNING` dice che il flusso
e' aperto, **non** che qualcuno ci sta suonando dentro: la misura che distingue le due cose e'
`hw_ptr`, che avanza solo se i frame passano davvero.

**IL SUONO E' SCATTOSO, e il ricampionamento era meta' del problema.** L'utente ha descritto
"un suono stutterato, non la vera canzone". Misurato con il tono continuo e una metrica oggettiva --
il picco del dispositivo di uscita di Windows campionato ogni 100 ms per dieci secondi, contando i
campioni a zero, che con un tono continuo sono buchi:

| audiodev | campioni a zero | picco medio |
|---|---|---|
| `dsound,id=a0` (com'era) | 37% | 0,0249 |
| `dsound,id=a0,out.fixed-settings=off` | 14% | 0,0360 |
| **`dsound,out.fixed-settings=off,out.buffer-length=30000,timer-period=2500`** | **0%** | **0,0425** |
| `dsound` + `out.buffer-length=200000` + `timer-period=5000` | 63% | 0,0142 |
| `sdl,id=a0,out.fixed-settings=off` | 42% | 0,0206 |

La causa della prima riga si vede nelle tracce: `dsound_wave_format nSamplesPerSec=44100` e
`audio_voice_pair out: hw=s16/44100/2 sw=s16/48000/2`. QEMU apriva l'uscita a 44100 fissi e
**ricampionava** i 48000 del guest a ogni frame. Con `out.fixed-settings=off` apre alla frequenza
del guest e il ricampionamento sparisce: i buchi si dimezzano abbondantemente.

Le altre due righe sono tentativi **scartati con la stessa misura**, e stanno qui perche' senza
questa tabella qualcuno li riproverebbe: un buffer da 200 ms con timer a 5 ms peggiora di piu' del
doppio, e il backend `sdl` (che pure passa da WASAPI invece che da DirectSound) sta in mezzo.

**RISOLTO, e la causa vera l'ha detta l'utente descrivendo il sintomo:** non era uno stutter, era
*"uno scatto ripetuto sempre uguale, la canzone non procede"*. Quello e' un buffer che si rilegge,
non un suono che salta. La misura che lo conferma sta nel guest:

```
/proc/asound/card0/pcm0p/sub0/hw_params:  period_size 256 frame, buffer_size 1024 frame
/proc/asound/card0/pcm0p/sub0/status:     appl_ptr 1024, hw_ptr 0 -- e hw_ptr NON avanza
```

**Il guest apre il PCM con 21 millisecondi di buffer.** Dentro quella finestra l'host deve
restituire i completamenti; se non ce la fa, il guest resta bloccato con `hw_ptr` fermo e
DirectSound continua a rileggere gli 85 ms che ha dentro. Registrando su file si vede la stessa
cosa da un'altra angolazione: 0,77 secondi di audio in dieci secondi di riproduzione, cioe' il 7%
del tempo reale, con i campioni corretti (ampiezza 16384, esattamente il guadagno 0,5 del tono) e
**nessuna ripetizione dentro il file**. L'anello non era nei dati: era nel buffer della scheda.

Il rimedio e' quindi l'opposto dell'istinto: **il buffer dell'host va reso PICCOLO, non grande**,
e il timer veloce, per stare dentro i 21 ms del guest. Con
`out.buffer-length=30000` e `timer-period=2500` i buchi vanno a **zero** e il picco diventa
costante (0,0425 medio contro 0,0428 massimo: praticamente una retta). Il buffer da 200 ms provato
prima faceva il contrario -- affamava il guest -- ed e' la riga peggiore della tabella.

### Dove si e' fermata la caccia, con i numeri in mano

Con le tre opzioni la canzone **prosegue** per una ventina di secondi, con qualche buco, e poi
torna a ripetersi. La misura che spiega il "non procede" e' l'avanzamento di `hw_ptr` nel guest,
campionato ogni cinque secondi per un minuto e mezzo:

```
t+15s .. t+90s   avanzamento 41000-45000 frame ogni 5 s
                 ne servirebbero 240000 (48000 al secondo)
                 cioe' il 17-19% del tempo reale, COSTANTE
```

Il flusso non si ferma: **scorre a un quinto della velocita'**. Ecco perche' il tempo della
canzone avanza (lo decide l'app) mentre il suono resta indietro e la scheda ripete cio' che ha.

**E non e' mancanza di potenza, misurato:**

| | |
|---|---|
| CPU del guest mentre suona | `600%cpu 4%user 16%sys **568%idle**` — praticamente scarico |
| CPU dell'host per QEMU | 1,2 s in 5 s su 10 core |

Nessuno dei due lati e' saturo, e il rapporto e' **costante** (8600 frame al secondo contro 48000):
non e' jitter, e' un limite. Il sospetto va quindi sul giro fra `virtio_snd_pcm_out_cb` e il
sottosistema audio, con il buffer da 21 ms che il guest impone.

**Il prossimo passo, preciso:** strumentare `virtio_snd_pcm_out_cb` per contare i **byte al
secondo** effettivamente scritti con `audio_be_write`, e confrontarli con i 192000 B/s che servono
a 48 kHz stereo 16 bit. Il rapporto dira' se il limite e' per chiamata (quante ne arrivano) o per
quantita' (quanto accetta ognuna) -- e sono due difetti diversi con due rimedi diversi. La
diagnostica per farlo e' gia' innestata, basta aggiungere il conteggio.

**La diagnostica che ha chiarito tutto resta disponibile**, dietro `WINQ_SND_DIAG=1`, e la sua
patch e' versionata in `qemu/patches/qemu-diagnostica-virtio-snd.patch`: stampa quante volte il
guest bussa sulla coda, quante volte la callback di uscita viene chiamata e con quanto spazio, e
quanti buffer tornano indietro. Senza quelle tre righe si continuava a discutere di ipotesi.

## Limiti dichiarati

- **`system_server` muore sotto carico di rete, e si porta via tutto.** Visto **due volte** il
  , la seconda mentre Spotify riproduceva musica (l'audio si sentiva: e' morto dopo la
  prima parola). Il crash e' nel processo di sistema, non nell'app:
  `*** FATAL EXCEPTION IN SYSTEM PROCESS: ConnectivityServiceThread`,
  `java.lang.RuntimeException: Unknown error` da `BinderProxy.transactNative` dentro
  `Messenger.send` chiamato da `ConnectivityService.callCallbackForRequest`. Tutti gli altri
  processi seguono con `DeadSystemException: The system died`. Dal basso il kernel dice
  `binder_alloc_buf size 1056768 failed, no address space` e
  `transaction ... failed 419409/29201/-28` -- dove 29201 e' `0x7211` = `_IO('r',17)` =
  **`BR_FAILED_REPLY`** e -28 e' ENOSPC: una transazione binder oltre il megabyte che il buffer
  per processo non puo' contenere. Android si riprende da solo (il launcher torna, e l'audio resta
  sano), ma le app aperte muoiono.

  **TERZA OCCORRENZA, e insegna piu' delle prime due.** Stessa eccezione Java, ma
  sul thread **`PackageManager`**, dentro `dispatchPackageBroadcast`, durante un `adb install`.
  Attenzione a come NON si legge: le righe del kernel di quella sessione
  (`cannot find target node`, `-22`) hanno per chiamante i pid **1186 e 202**, mentre
  `system_server` era il **397** -- sono app che chiamano dentro un `system_server` **gia' morto**,
  cioe' la cascata e non l'innesco. E il codice che le accompagna, 29189, e' `0x7205` =
  `_IO('r',5)` = **`BR_DEAD_REPLY`**, che Android traduce in `DeadObjectException`, cioe'
  l'eccezione **catturabile**: non puo' essere la causa di una `RuntimeException` non catturata.
  Attribuirle al crash e' un errore che qui e' stato commesso e ritirato lo stesso giorno.

  **Cosa e' stabilito, e cosa no.** Stabilito: in tutte e tre le occorrenze un fallimento binder
  arriva a Java come `RuntimeException` invece che come `DeadObjectException`, quindi cade fuori
  dai `catch (RemoteException)` del framework e uccide il processo di sistema. NON stabilito: la
  transazione che fallisce. Per non ne esiste prova a livello di kernel; per la
  coppia `BR_FAILED_REPLY`+ENOSPC e' coerente ma nel registro non e' annotato **quale pid** l'abbia
  emessa. **I due eventi condividono la conseguenza, non la causa**, e chiamarli "la stessa
  famiglia" era generoso.

  **C'era un ostacolo di metodo, non di codice, e e' stato rimosso.** Le prove si
  distruggevano a ogni avvio: `vm.c` cancellava `guest/logs/sessione-viva.log` in `vm_apri` (per
  non leggere un `boot_completed` di due ore prima) e `registro.c` apriva il registro con `"w"`
  (per non prendere per nuovo un messaggio del penultimo avvio). Due scelte **giuste** che insieme
  rendevano impossibile indagare un guasto che capita una volta ogni qualche ora: i dati
  sono sopravvissuti solo perche' letti a mano prima del riavvio successivo. Ora `archivio.c`
  **sposta** i due file invece di distruggerli (vedi la sezione sull'archivio piu' sotto), e il
  file attivo nasce pulito come prima.

  **Come si indaga la prossima occorrenza, in ordine.** Quando `system_server` muore, il guest si
  riprende: si ha tempo, ma **non** dopo un riavvio, perche' `logcat` del guest e' in memoria.
  1. subito, senza riavviare niente: `adb logcat -b crash -d > crash.txt` e
     `adb shell dmesg > dmesg.txt`;
  2. dalla traccia Java si prende il **pid** di `system_server` (la colonna dopo la data);
  3. si cercano nella seriale **solo** le righe binder di QUEL pid: `binder: <pid>:`. Le righe di
     altri pid sono la cascata, e leggerle come causa e' l'errore che qui e' stato commesso;
  4. si decodifica il codice: `0x7211` = `BR_FAILED_REPLY` (il fallimento **non** catturabile),
     `0x7205` = `BR_DEAD_REPLY` (che diventa `DeadObjectException`, catturabile). Il primo e' la
     pista, il secondo no;
  5. e si guarda `size X-Y`: nel 01/08 era oltre il megabyte (una transazione troppo grande), il
     04/08 erano 104 byte (la dimensione non c'entrava).

- **L'audio esce dal guest ma nessuna app dell'immagine lo produce**: vedi la sezione dell'audio qui sotto. Il difetto non e' nell'audio, e' che `com.android.providers.media.module` muore in ciclo.
- **RISOLTO -- la memoria esterna non si montava, e la causa era una riga di manifest.** Il sintomo era una fila: `emulated;0 unmountable`, `/sdcard` con `Permission denied`, MediaProvider morto in ciclo, niente `content://media`, niente suonerie, niente screenshot, e Spotify che sembrava andare in crash (era un ANR). **Una sola causa:** `/vendor/etc/vintf/manifest.xml` dichiarava `android.hardware.media.omx@1.0` con `IOmx` e `IOmxStore`, e in `/vendor/bin/hw/` quel servizio **non esiste**. Chi chiedeva la lista dei codec restava appeso su hwservicemanager: lo stack dell'ANR lo dice esatto -- `MediaProvider.onCreate` -> `TranscodeHelperImpl.hasHDRPlugin` -> `MediaCodec.createDecoderByType` -> `MediaCodecList::getInstance` -> `BpMediaPlayerService::getCodecList` -> `ioctl` sul binder, per sempre. Senza `onCreate` niente sessione FUSE, senza sessione il volume non si monta, e il provider veniva riavviato in ciclo. **Il rimedio** e' togliere quella dichiarazione dal manifest del vendor: i codec software ci sono gia' altrove (APEX `com.android.media.swcodec` e `manifest_media_c2_software.xml`), quindi non si perde nulla. Dopo: `emulated;0 mounted`, `/sdcard` scrivibile, MediaProvider vivo, screenshot che funzionano, Spotify in primo piano con zero ANR. `vendor.img` e' stata modificata; la copia precedente e' `guest/images/android/riserva-mesa26.2/vendor.img.pre-omx`.
- **Nessun installer**: si spedisce una cartella (`runtime/bin/`), non un pacchetto. Nessuna voce di registro, nessun collegamento creato in automatico.
- **I bottoni hardware costano 100-300 ms l'uno**: ogni pressione lancia un processo `adb.exe` nuovo (vedi `app/guscio/adb.c`). Non e' stato misurato se questo dia fastidio all'uso reale: una sessione `adb shell` persistente per abbatterlo e' rimandata a dopo la misura.
- **La scadenza di Windows sullo spegnimento e' misurata di misura, non con margine.** Vedi la sezione sopra: 4485 ms contro pochi secondi concessi da Windows. Un margine stretto, non un margine comodo.
- **L'evento con nome che segnala la chiusura della finestra di Android muore col processo del guscio.** Se il guscio muore PRIMA che l'utente chiuda quella finestra, la chiusura successiva ricade sul comportamento duro di prima di questo lavoro (vedi "il guscio muore" sopra). Non e' un peggioramento, ma non e' protetto.
- **La rotazione resta un ripiego software** (vedi sopra): il device lato host fa la propria parte, il guest no, e la causa non e' nota.
- **La rotella non scorre ancora**: arriva in Android come `SCROLL` ma alla posizione `(0,0)` del puntatore. Vedi la sezione sopra: serve mandare anche il movimento, ed e' una scelta di prodotto non ancora presa.
- **Il gesto del trascinamento non e' provato in automatico**, e non lo sara': un `WM_DROPFILES` sintetico da un altro processo non e' fattibile in modo sicuro, perche' l'`HDROP` vale solo nello spazio del processo che l'ha creata. Tutto il resto di quel percorso e' verificato. **Tentativo, e perche' e' fallito:** si e' provato a guidare un trascinamento VERO da un programma sorgente OLE (`IDataObject` con `CF_HDROP` piu' `IDropSource`, movimento con `SendInput`) invece di sintetizzare il messaggio. Non ha funzionato, e la ragione e' istruttiva: `DoDragDrop` era chiamata da un processo **senza finestra**, dopo aver iniettato il tasto premuto sopra la finestra di qualcun altro, e il suo ciclo modale traccia il gesto a partire da una finestra del thread chiamante. Si aggiusterebbe creando una finestra sorgente vera. La strada non e' chiusa come quella del messaggio sintetico: e' solo non finita.
- **Rilasciare sulla finestra di ANDROID non fa niente**, ed e' per progetto: solo la finestra del guscio ha `WS_EX_ACCEPTFILES`. Il rilascio arriverebbe a `winq`, dentro QEMU, e il tubo con nome esiste solo nel verso guscio -> winq, quindi servirebbe un canale nuovo per una funzionalita' che sul guscio si fa senza.
- **Il rifiuto del rilascio durante l'avvio e' provato in unita', non col gesto.** La finestra utile dura trentacinque secondi e il rilascio a mano e' arrivato tre minuti dopo. La prova esiste ed esercita tutti e otto gli stati non pronti (`test-rilascio`), e si puo' fare per una scelta di progetto: lo stato ARRIVA a `rilascio_avvia` come parametro invece di essere letto con `vm_stato()` dentro il modulo.

## Le trappole

Ognuna col sintomo prima della causa: e' cio' che si vede prima di sapere.

**Il pannello EDIT si fermava in silenzio al 15% di un avvio.** Un controllo EDIT di Win32 senza `EM_SETLIMITTEXT` esplicito accetta di default solo 30000 caratteri; oltre, ogni `EM_REPLACESEL` viene rifiutato SENZA errore. Un avvio completo produce **201344 caratteri** di seriale: il pannello smetteva di aggiornarsi intorno al 15%, senza che nulla lo segnalasse. Peggio di un pannello lento: `registro_nuove` marca le righe come consegnate non appena le copia nel buffer, quindi le righe arrivate dopo la saturazione del controllo non erano solo in ritardo, erano PERSE per sempre. **Il tetto del controllo e la soglia di troncamento applicativo devono essere due numeri DISTINTI**: una prima correzione li aveva legati alla stessa macro, e con un solo valore `GetWindowTextLengthA` non puo' mai superare il tetto del controllo, quindi la condizione di troncamento (`len > soglia`) non e' mai vera -- il troncamento diventa codice morto, e lo si scopre solo con una prova che scriva davvero oltre la soglia.

**`FindWindowA` con la classe del guscio ritornava handle 0**, in questo stesso ambiente, mentre `EnumWindows` la trovava senza problemi con lo stesso confronto di classe. Non e' un difetto della finestra (la classe e' registrata correttamente, come conferma l'enumerazione): e' l'API di ricerca per nome che qui si e' mostrata inaffidabile. Ogni verifica di questo documento -- comprese quelle di questo stesso task -- usa `EnumWindows`, mai `FindWindowA`.

**Il pannello di log e' leggibile da un processo esterno -- col metodo giusto, non con qualunque metodo.** Un tentativo di leggere il testo del controllo EDIT con un P/Invoke di `GetWindowTextLengthA`/`GetWindowTextA` (`CharSet=Auto`), da uno script di verifica cross-processo, ha ritornato sistematicamente lunghezza zero / stringa vuota. La causa NON e' UIPI (User Interface Privilege Isolation): se UIPI bloccasse la lettura del testo, bloccherebbe anche `SendMessageA` con `WM_GETTEXTLENGTH`/`WM_GETTEXT` fra gli stessi due processi -- e invece quella via FUNZIONA. una misura l'ha misurato (`.superpowers/sdd/dpi-task-2-report.md`): passando da `GetWindowText` a `SendMessageA` esplicito con `WM_GETTEXTLENGTH`/`WM_GETTEXT` ha letto e trascritto la prima riga vera del pannello dall'esterno. La causa e' quindi il METODO: il P/Invoke con `CharSet=Auto` di `GetWindowTextLengthA`/`GetWindowTextA` non funziona in modo affidabile su un controllo di un altro processo; `SendMessageA` con `WM_GETTEXTLENGTH`/`WM_GETTEXT` si'. La stessa cosa e' successa altrove usando il metodo che non funziona, senza collegare le due cose. Chi deve leggere il pannello dall'esterno usi `SendMessageA` con `WM_GETTEXTLENGTH`/`WM_GETTEXT`, non `GetWindowText`.

**L'evento con nome muore col guscio.** Se il guscio e' gia' morto quando l'utente chiude la finestra di Android, `winq_segnala_chiusura` non trova piu' l'evento (nessun altro processo ne tiene una maniglia), e si ricade sull'uscita dura -- non peggio di prima del guscio, ma non protetto da esso. Vedi "il guscio muore" sopra per i due tempi misurati.

**`guest/logs/qemu-stderr.log` non viene mai scritto sul percorso del guscio.** Quel file lo scrive solo `avvia-android.ps1`, con `-RedirectStandardError`. Il guscio cattura lo stdout/stderr di QEMU con un tubo proprio, verso il registro interno (`vm_thread_stderr_corpo`): cercare quel file dopo un avvio col guscio non lo trova mai, per progetto, non per un difetto.

**La rotazione richiede DUE cose, non una.** Scambiare framebuffer (larghezza/altezza) non basta: Android non si riconfigura da se' in risposta all'hotplug del virtio-gpu, in questa immagine e con questo kernel. Serve anche `user_rotation` via adb (vedi sopra). Punto aperto: perche' il guest ignori `VIRTIO_GPU_EVENT_DISPLAY` non e' noto.

**Il tubo dei comandi funzionava una volta sola per sessione, e il difetto era invisibile.** Il tubo con nome e' creato con `nMaxInstances = 1`; il guscio apre e chiude un handle client a ogni comando, ma senza `DisconnectNamedPipe` lato server l'istanza resta agganciata a un client sparito. Dal secondo comando in poi, `CreateFileA` falliva con `ERROR_PIPE_BUSY` (231) -- ma la rotazione funzionava comunque, perche' ricadeva sul ripiego adb (`user_rotation`), che non passa dal tubo. Un canale morto mascherato da una funzione che gira: non si poteva scoprire provando il risultato, serviva leggere il codice. MISURATO dopo la correzione (`DisconnectNamedPipe` dopo ogni comando): tre pressioni consecutive del bottone "ruota" hanno dato 3 conferme del tubo (`"risoluzione del guest -> ... a 60 Hz"`), 0 fallimenti (`"il tubo dei comandi non risponde"`).

## Il DPI, e cosa della sua gestione non è provato

Il guscio dichiara `PER_MONITOR_AWARE_V2` come prima istruzione di `main()`, prima di `istanza_unica()`: la dichiarazione non ha effetto retroattivo su una finestra già creata, quindi «presto» non basta. Le tre funzioni (`SetProcessDpiAwarenessContext`, `GetDpiForWindow`, `GetDpiForSystem`) si risolvono con `GetProcAddress` su `user32.dll` e non per collegamento diretto: un import non risolvibile fa rifiutare dal caricatore il processo **intero**, quindi il ripiego non sarebbe «sfocato» ma «non parte». Se manca la prima, si prosegue senza consapevolezza e **lo si dice** nel registro — `Habumi.exe --config` lo mostra senza avviare la VM.

Il layout vive in una funzione sola (`fin_disponi`), che riceve il DPI e scala ogni costante: la creazione, `WM_SIZE` e `WM_DPICHANGED` la chiamano tutte e tre. Il **carattere** si scala con i controlli, o crescono loro e il testo no — il sintomo sarebbe bottoni grandi con scritte minuscole.

**`WM_DPICHANGED` è implementato, e non è provato — nemmeno in simulazione.** Non è una mancanza di un secondo monitor: `WM_DPICHANGED` porta il rettangolo suggerito come un **puntatore** in `lParam`, e il messaggio non è marshallato. Una `SendMessage` da un processo esterno — l'unico modo di "simularlo" dal di fuori, come farebbe uno script di verifica — farebbe dereferenziare al guscio un indirizzo valido solo nello spazio di memoria del *mittente*: letture di spazzatura, o un crash. Farlo per davvero costerebbe una di due cose: codice di prova scritto **dentro** il prodotto apposta per l'occasione, oppure `VirtualAllocEx` + `WriteProcessMemory` per piazzare il `RECT` nello spazio del guscio da fuori — scrivere nella memoria di un processo vivo per il solo scopo di provarlo. Nessuna delle due è stata pagata per un gestore di quattro righe. Quello che resta vero: il gestore c'è (`fin_wndproc`, ramo `WM_DPICHANGED`, in `app/guscio/finestra.c`), applica il rettangolo suggerito con `SetWindowPos` e richiama `fin_disponi` sul client risultante — lo stesso percorso già verificato di `WM_SIZE`. Il sintomo, se qualcosa non regge, sarà visibile subito al primo monitor con scala diversa collegato — finestra sfocata o della dimensione sbagliata — e non silenzioso.

**Un secondo limite dichiarato:** la scala per `scala_guest` si legge con `GetDpiForSystem`, cioè dallo schermo primario, perché la risoluzione del guest si decide prima che la finestra di Android esista. Su più schermi con scale diverse quel valore può non essere quello dello schermo su cui la finestra finirà.

## La mappatura dei tasti

Tastiera tradotta in tocchi dentro il guest: `WASD` diventa un joystick virtuale
trascinato, ogni altro tasto assegnato tocca un punto fisso dello schermo. Il
mouse non c'entra (vedi "Scartato" nella spec). Si usa dal guscio: il bottone
«tasti» apre un menu coi profili trovati in `runtime/keymaps/*.txt`, con «spegni»
e le due voci del modo impara (disabilitate finche' non si e' mai scelto un
profilo). La scorciatoia di sistema `Ctrl+Alt+T` (`RegisterHotKey`) accende o
spegne l'ultimo profilo scelto anche senza il fuoco sulla finestra del guscio,
che non lo ha mai. Nel modo impara si preme il tasto (o si clicca il centro e
poi il bordo, per il joystick) e si clicca il punto sullo schermo di Android:
la riga si scrive o sostituisce da sola, senza salvataggio a mano.

### Il numero misurato, e cosa copre

La spec (sezione 14) dichiarava la latenza aggiunta "non misurata". Il piano originale
chiedeva di strumentare QEMU con due `QueryPerformanceCounter` (uno in
`winq_mappa_intercetta`, uno nel drenaggio della coda). Non l'ho fatto, per
decisione dell'utente: dopo la traduzione il percorso di un tocco sintetico e'
bit per bit lo stesso di un dito vero -- stessa `winq-coda`, stessa iniezione
`qemu_input_*` -- quindi il solo costo che la mappatura AGGIUNGE e' la
traduzione stessa (`mappa_tasto`), non il resto del percorso che si paga
comunque, mappa accesa o no.

Misurato quindi nel modulo puro, senza VM: un programma a getto (compilato con
gli stessi flag delle prove, `clang -std=c11 -Wall -Werror`, e cancellato subito
dopo, non entra nel repo) crea una mappa PIENA -- 32 tocchi piu' il joystick,
il caso peggiore per la ricerca lineare in `mappa_tasto` -- e chiama
`mappa_tasto` sull'ultimo tasto letto (quello che costa la scansione piu'
lunga: 4 confronti mancati sul joystick, poi 31 mancati e 1 trovato sui 32
tocchi), alternando pressione e rilascio, 5.000.000 di volte per corsa:

```
94-99 ns per chiamata (quattro corse, media ~96 ns)
```

**Cosa copre:** solo la ricerca e la produzione delle azioni dentro
`mappa_tasto`, sulla mappa piu' grande possibile. **Cosa NON copre, e perche'
non serve:** il resto del percorso -- l'accodamento in `winq-coda`, il
drenaggio sul ciclo principale, `qemu_input_*` -- non e' cronometrato perche' e'
identico a quello di un dito vero: non e' un costo che questa funzione
aggiunge, e cronometrarlo misurerebbe QEMU, non la mappatura. Sotto il
millisecondo per un ordine di grandezza abbondante: irrilevante rispetto a un
frame (16,7 ms a 60 Hz).

### Le prove gia' fatte sul prodotto vivo (dal rapporto di quella verifica)

Mappa `joystick WASD 200 750 100` + `tocco Q 250 700`, tasti iniettati con lo
scancode vero (vedi "Le trappole" piu' sotto) e letti con `getevent -lt` nel
guest:

| tasto | dove arriva | cosa produce |
|---|---|---|
| `W` (mappato) | `event1` (multitouch) | `BTN_TOUCH` DOWN, `X=0x1999 Y=0x5fff` (il centro), poi `Y=0x5332` (centro meno il raggio, l'`UPDATE`), poi UP |
| `W` | `event0` (tastiera) | **assente**: il tasto mappato non arriva piu' come tasto |
| `Z` (non mappato) | `event0` (tastiera) | `KEY_Z` DOWN poi UP, com'era prima |

I numeri combaciano col design, non sono dedotti dal codice: asse del tocco
0..32767, centro 200/1000 * 32767 = 6553 = `0x1999` e 750/1000 * 32767 = 24575
= `0x5fff`; dopo l'`UPDATE`, 650/1000 * 32767 = 21298 = `0x5332`.

**Attenzione a rifare questo conto: vale solo senza bande nere.** I permille
della mappa sono permille dell'**area cliente**, non del guest, e il conto qui
sopra li tratta come se fossero la stessa cosa. Ha combaciato perche' in quel
momento l'area cliente e la superficie del guest avevano le stesse proporzioni,
quindi la mappatura fra le due era uno a uno. Con proporzioni diverse -- guest
ruotato, finestra trascinata a una forma che non e' quella del guest --
`winq_win_to_guest` inserisce le bande nere e i valori attesi cambiano: il
punto va prima portato da permille dell'area cliente a pixel cliente, poi da
pixel cliente a coordinate del guest, e solo allora moltiplicato per 32767.
Rifacendo la verifica con la scorciatoia di sopra su una finestra con bande
nere, i numeri non torneranno e sembrera' un guasto della mappatura.

### Limiti dichiarati (spec sezione 14)

- **Solo tastiera.** Niente mouse per la mira, niente gamepad.
- **Una mappa vale per un orientamento.** Ruotando i punti si spostano, ed e'
  corretto perche' si sposta anche l'interfaccia del gioco: non ci sono due
  mappe per profilo.
- **`Ctrl+Alt+T` e' tolta a tutto Windows**, non solo al guest.
- **Un joystick per profilo.** I giochi con due stick non sono coperti.
- **Niente colpetto automatico ne' macro.** Un tasto = un dito, finche' e'
  premuto.
- **Il profilo si sceglie a mano.**
- **La latenza aggiunta dalla traduzione non era misurata** -- vedi sopra: ora
  lo e', e copre solo `mappa_tasto`.

### Le trappole, e il perche'

**Le diagonali vanno normalizzate.** Senza (fattore 707/1000, circa
1/sqrt(2), nel codice), `W`+`D` darebbe 1,41 volte il raggio e il dito uscirebbe dall'area
dello stick: le quattro direzioni pure funzionerebbero e le diagonali no, e
sembrerebbe un difetto del gioco invece che della mappatura.

**La ripetizione automatica di Windows va scartata.** Senza lo scarto (bit 30
di `LPARAM`) dentro `mappa_tasto`, un tocco mantenuto diventerebbe una raffica
di `BEGIN` invece di un `BEGIN` seguito da `UPDATE`. E scartarla non basta da
sola: la ripetizione scartata ritorna zero azioni, e un chiamante che guardasse
solo quel numero crederebbe "tasto non mio" e lo consegnerebbe al guest --
tenere premuto `W` avrebbe scritto `wwwww` dentro il gioco. E' il buco trovato
nell'autorevisione del piano, chiuso con `mappa_tasto_e_mio`: il chiamante la
interroga a parte per sapere se il tasto e' della mappa, indipendentemente da
quante azioni l'ultima chiamata ha prodotto.

**Ogni spegnimento della mappa deve rilasciare i diti.** Perdita di fuoco
(`WM_KILLFOCUS`), cambio di profilo, `mappa spegni` -- e l'ingresso in modo
impara, che e' un quarto punto di spegnimento e non un dettaglio dell'apprendimento.
L'ultimo dei tre era un difetto vero, trovato in revisione e non nel brief
originale: `winq_mappa_gestisci_impara` spegneva la mappa senza chiamare
`mappa_rilascia_tutto` come gli altri tre punti. Provato dal vivo: tasto
mappato tenuto giu', comando `mappa impara` mandato un secondo dopo, l'`UP`
nel guest arriva a quell'istante (T+1,05 s) e non a T+4 quando il tasto viene
davvero rilasciato -- prova che senza la correzione sarebbe rimasta ambigua se
i due istanti non fossero stati separati nel tempo.

**I comandi del tubo si postano, non si mandano.** `winq-ciclo.c` li legge sul
thread del ciclo principale; la mappa vive sul thread della finestra. Stessa
regola gia' in vigore per `WINQ_MSG_DIMENSIONA`: `PostMessageA` con
`WINQ_MSG_MAPPA_CARICA`/`_SPEGNI`/`_IMPARA`, mai `SendMessage`, che
bloccherebbe il chiamante finche' il thread della finestra non serve il
messaggio. La stringa del percorso e' allocata da chi posta (`g_strdup`) e
liberata da chi riceve, in ogni ramo -- compreso l'annullamento di un modo
impara lasciato a meta'.

**`winq_key` converte lo SCANCODE, non il virtual-key.** Legge i bit 16-23 di
`LPARAM`. Chi inietta tasti sintetici per provare (con `PostMessage`, non con
la tastiera vera) deve mettere lo scancode vero -- `W=0x11`, `Z=0x2C` -- o il
tasto viene scartato in silenzio e sembra rotto il prodotto invece che sbagliata
la prova.

### Due buchi del progetto, trovati facendo questo lavoro (non riguardano la mappatura)

**`qemu/scripts/build-qemu.sh` si ferma a `ninja` e non schiera il binario**
in `runtime/bin/qemu-nostro.exe`. Chi compila e poi prova, prova il binario
VECCHIO -- ed e' costato una diagnosi sbagliata durante il quella verifica (`comando
non riconosciuto sul tubo`, con la causa vera che era un `qemu-nostro.exe` del
04/08 ancora in uso). Lo schieramento va fatto a mano; la riserva del binario
precedente e' `runtime/bin/qemu-nostro.exe.prima-mappa`.

**Questa build di QEMU non ha `-Werror` davvero attivo.** L'albero viene da un
tarball senza `.git` (QEMU abilita `-Werror` di default solo per alberi con
`.git`) e `build-qemu.sh` non passa `--enable-werror`. "Compila senza avvisi"
quindi non garantisce niente da solo: i file toccati vanno verificati a parte
con un `-Werror` vero, con gli stessi flag esatti della build reale (come
fatto in quelle verifiche, in una cartella scartata poi rimossa).

## Il selettore di varianti

Due installazioni di Android che coesistono, ciascuna col proprio `/data`: la
**vanilla** (senza GAPPS) e la **GAPPS**. Non e' un installer che converte
un'installazione nell'altra: sono due installazioni tenute pronte insieme, e
si sceglie quale avviare. Il costo e' il disco (vedi sotto); il vantaggio e'
che passare da una variante all'altra non distrugge niente.

**Come si usa.** Un bottone **variante** (`ID_VARIANTE`, il nono della
finestra) accanto a «tasti» e «ruota». Il clic apre un menu che classifica UNA
SOLA volta l'altra variante (quella non attiva), guardando solo il disco, mai
la rete:

```
vanilla (attiva)
GAPPS -- pronta
```

oppure, se l'immagine dell'altra variante non c'e' ancora o ha un'impronta
diversa da quella registrata:

```
vanilla (attiva)
GAPPS -- da scaricare, 1,17 GB
```

Scegliere una variante **gia' pronta** ferma la VM, scrive `variante=` in
`runtime/bin/config.txt` e riavvia. Scegliere una variante **da
scaricare** chiede conferma nominando i byte, scarica con l'avanzamento nel
pannello del registro (una riga ogni 5 punti percentuale, non a ogni blocco:
sarebbero circa 18000 righe per 1,17 GB), estrae con `tar.exe`, verifica lo
sha256, e **solo se tutto e' andato** cambia variante e riavvia. Mentre uno
scaricamento e' in corso il menu diventa a una voce sola, "annulla lo
scaricamento". Tutto il lavoro di rete (manifesto, scaricamento, estrazione,
cancellazione dell'archivio, scrittura di `runtime/variants.txt`) gira su un
thread suo, mai su quello della finestra; il cambio VERO -- fermare la VM,
scrivere la configurazione, riavviare -- resta invece sul thread della
finestra, perche' `vm.c` non ha alcun lucchetto sul proprio stato e va
chiamato sempre da li'.

### La tabella variante -> file

| `variante` | immagine di sistema | dati |
|---|---|---|
| `vanilla` | `guest/images/android/system.img` | `guest/images/android/data.img` |
| `gapps` | `guest/images/android/system-gapps.img` | `guest/images/android/data-gapps.img` |

`vendor.img` e l'initramfs sono **gli stessi** per entrambe: le
personalizzazioni del progetto (nome GodziDroid, animazione, appunti,
`asound.conf`, permessi dei nodi) viaggiano tutte nell'initramfs e valgono per
tutte e due -- confermato dalla prova dal vivo (sotto): il nome del prodotto
e' GodziDroid in entrambe le varianti. Nessun file viene mai spostato o
sovrascritto per cambiare variante: cambia solo quale riga della tabella e'
attiva.

### La prova dal vivo (07-)

**Scaricamento e cambio, registrati:**
`lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip`, 1.171.824.911
byte, sha256 `7706b04e815802ac8918c71f1c364b647626f68ba281edf0164db4b5a4166d82`.
Scaricato in circa 38 s, estratto in circa 8 s. L'immagine estratta pesa
2.597.105.664 byte. L'archivio si cancella dopo l'estrazione riuscita.

**Il giro completo, tre cambi registrati:** vanilla (`system.img` +
`data.img`) -> GAPPS (`system-gapps.img` + `data-gapps.img`) -> vanilla di
nuovo (`system.img` + `data.img`).

**Le prove, non le supposizioni:**

| controllo | esito |
|---|---|
| pacchetti Google nel guest | 4 su GAPPS (fra cui `com.android.vending` e `com.google.android.gms`), 0 su vanilla |
| zygote | partito UNA VOLTA SOLA in entrambe, cioe' avvio sano in entrambe le varianti |
| nome del prodotto | GodziDroid in ENTRAMBE: le personalizzazioni viaggiano nell'initramfs e valgono per tutte e due |
| i due `/data` | **entrambi conservati** -- l'utente ha ritrovato le proprie app sia passando a GAPPS sia tornando a vanilla |

L'ultima riga e' la ragione d'essere di tutto il lavoro: senza, il selettore
sarebbe un ripristino di fabbrica travestito da menu.

**Il `/data` vuoto spedito col prodotto:** `runtime/empty-data.zip`,
4.190.749 byte, contiene un ext4 vuoto da 4 GiB. Si estrae quando una
variante viene scelta e il suo `/data` non c'e' ancora. Chiude un buco che
era a verbale: prima `data.img` si creava a mano dalla WSL, impossibile da
pretendere da chi installa.

**Lo spazio:** circa 12,3 GB per avere entrambe le varianti pronte (1,62 GB
per la vanilla, 2,60 GB per la GAPPS, 4,00 GB per ciascuno dei due `/data`).
Il **picco** durante lo scaricamento della GAPPS e' 7,8 GB, perche' l'archivio
scaricato convive con l'immagine gia' estratta finche' non viene cancellato.

### Il guasto che vale la pena raccontare

Al primo tentativo vero, il cambio verso GAPPS e' **fallito all'ultimo
passo**, con l'archivio gia' scaricato e verificato:

```
variante: estrazione di ...system.zip fallita: estratto
  HabumiDatiTmp\system.img ma non riesco a spostarlo su
  guest/images/android/system-gapps.img (5)
```

Errore 5 = accesso negato (`ERROR_ACCESS_DENIED` su `MoveFileExA`). La
diagnosi e' stata fatta con misure, non con ipotesi: la destinazione non era
di sola lettura; il thread di classificazione di questo prodotto era girato
una volta sola, a 5,5 s dall'avvio, mentre il guasto e' arrivato a 9471 s,
troppo tardi per essere lui; misurando il blocco sul file a intervalli dopo
un avvio, risultava bloccato a 3 s (mentre qualcosa ne calcola l'impronta) e
libero a 15 s e a 45 s -- a regime nessuno lo tiene. Riprovando A MANO lo
stesso cambio pochi minuti dopo, senza riscaricare (l'archivio era rimasto
sul disco), e' riuscito.

**Il perche':** qualcuno teneva la destinazione per un ISTANTE, quasi
certamente un antivirus che si sveglia su 2,6 GB appena scritti -- non
verificato direttamente, ma coerente con ogni misura fatta. Un conflitto di
condivisione su Windows e' **transitorio per natura**: e' esattamente il tipo
di errore per cui riprovare ha senso. **Il difetto vero non era il blocco,
era che il codice non gli sopravviveva**: buttava via un'operazione da
1,17 GB al primo no, invece di aspettare un istante e riprovare. Corretto
riprovando con pause crescenti -- 200, 400, 800, 1600, 3200 ms -- e solo per
i due errori transitori (accesso negato e conflitto di condivisione, 5 e 32):
su ogni altro errore si esce subito, senza aspettare. La stessa funzione
serve sia allo spostamento dell'immagine sia allo spostamento del `/data` da
4 GiB, un solo posto invece di due copie del ciclo di riprova.

### Limiti dichiarati (spec, sezione 10)

- **Due varianti, non N.** La tabella e' chiusa: aggiungerne una terza vuole
  toccare il codice, ed e' voluto finche' non ce n'e' una terza vera.
- **Circa 12,3 GB di disco** per avere entrambe le varianti pronte, con un
  **picco** di 7,8 GB durante lo scaricamento della GAPPS.
- **Il download e' un pezzo unico:** niente connessioni in parallelo, niente
  scelta del mirror.
- **`tar.exe` e' una dipendenza da Windows 11**, dichiarata e non aggirata.
- **La build si fissa e non si aggiorna da sola.** Il menu propone la voce
  piu' recente del manifesto solo al momento della scelta; da li' in poi
  `runtime/variants.txt` fissa quella build, e una build nuova a monte non
  cambia niente finche' qualcuno non sceglie di nuovo.
- **Non e' previsto disinstallare una variante** dal menu: si cancella il
  file a mano.
- **Il tempo del download non e' misurato**, e non e' misurabile in modo
  utile: dipende dalla rete e dal mirror.

### Le trappole

**Il `/data` va rifatto a ogni cambio di IMMAGINE, non di variante.** E' il
vincolo che ha disegnato tutto (spec, sezione 2, e "Il passaggio all'immagine
VANILLA" piu' sopra): un `/data` scritto da un'immagine diversa manda il
sistema in ciclo senza un messaggio che nomini `/data`. Qui i due `/data`
sono separati apposta, uno per variante, quindi **il selettore non tocca mai
questo bordo nell'uso normale**: la trappola resta per chi sostituisse un
file immagine a mano, fuori dal selettore.

**Gli URL di SourceForge rimandano a un altro host.** Finiscono in
`/download` e rimandano a un mirror: uno scaricatore che non segue il
rimando scarica una pagina HTML e la chiama immagine. Lo scaricatore imposta
sempre `WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS`, esplicito invece che affidato
al default.

**Il file si rinomina solo dopo la verifica.** `<nome>.parziale` diventa il
nome definitivo solo dopo che lo sha256 combacia; se non combacia si
cancella. Un file a meta' non deve mai poter sembrare un'immagine buona.

### Due buchi di metodo, validi per tutto il progetto e non solo per questa funzione

**`app/scripts/test-guscio.sh` compila una suite per volta, e ogni suite
dichiara i propri moduli: puo' essere tutta verde mentre il PRODOTTO non
collega.** E' successo durante questo lavoro: tutte le suite verdi e
`build-guscio.sh` fermo su `undefined symbol: var_da_testo`, perche' lo
script di build non era stato aggiornato per collegare il nuovo modulo
`varianti.c`. Chi aggiunge un modulo lancia ANCHE `build-guscio.sh`, non solo
`test-guscio.sh`.

**Questo checkout ha `core.autocrlf` attivo: un `grep` sul file nell'albero
di lavoro puo' mostrare CR che nel commit NON ci sono.** La verifica di ASCII
e fine riga va fatta sul BLOB, non sul file su disco:
```
git show HEAD:percorso | tr -dc '\r' | wc -c
```

**E una terza, piu' importante di entrambe: le 83 prove del lettore del
manifesto erano verdi mentre il lettore non funzionava sul manifesto VERO.**
Le prove usavano JSON compatto scritto a mano (`"datetime":1775188491`,
senza spazio); il manifesto reale, scaricato da GitHub, ha uno spazio dopo i
due punti (`"datetime": 1775188491`), e il lettore si fermava proprio su
quello spazio. **Una prova costruita sul formato che immaginiamo non prova il
formato che arriva.** Corretto saltando gli spazi bianchi dopo ogni chiave,
con una prova scritta copiando l'estratto autentico del manifesto parola per
parola.

## Riferimenti

- `qemu/HOST-WINDOW.md` -- il backend `-display winq`, la finestra dove Android disegna
- `app/guscio/guscio.h` -- i tipi e le firme condivise fra i moduli del guscio
- `.superpowers/sdd/gu-task-8-report.md` -- il rapporto di questo task, coi numeri letterali di questa verifica

- `.superpowers/sdd/sv-task-1-report.md` .. `sv-task-6-report.md`, `sv-riprova-report.md`, `sv-task-7-report.md` -- i rapporti del selettore di varianti, con le prove passo per passo

## L'HAL audio fuori dall'immagine: vendor.img torna vergine

`vendor.img` era **l'ultima immagine del progetto che non fosse vergine**, e nessuno se n'era
accorto: conteneva l'HAL audio con l'anello da 4096 frame, installato per togliere
lo stutter sotto carico. Da oggi quel file viaggia nell'initramfs, come tutto il resto, e
l'immagine si puo' scaricare da monte invece di essere redistribuita.

**Come si e' scoperto**: preparando la release, confrontando `vendor.img` con la sua riserva
pre-HAL. md5 `5f3b55bc...` contro `b8af3acc...`. Nessuna prova poteva trovarlo, perche'
nessuna prova guarda dentro le immagini.

### La misura che ha smontato l'ipotesi

`guest/scripts/installa-hal-4096.sh` dichiara in cima: «Due byte, verificati con `cmp -l`:
il campo `imm16` di due istruzioni `MOVZ` passa da `0x1000` a `0x4000`». Su quella base il
piano era **rifare la modifica all'avvio**, senza redistribuire niente.

Estraendo pero' `lib64/hw/audio.primary.waydroid.so` dalle due immagini:

```
vendor.img.riserva-prehal   19.880 byte
vendor.img                  19.664 byte
cmp -l                      14.296 byte diversi
```

Non sono due byte, ed e' cambiata anche la dimensione: il `.so` e' **ricostruito**. Lo
stesso copione lo diceva piu' in basso -- «ora il `.so` e' ricostruito da» -- mentre il
commento in cima descriveva ancora il primo tentativo. **Ha vinto la misura, non il
commento**, e l'ipotesi approvata e' stata buttata prima di scrivere una riga di codice.

### Come funziona adesso

Il file, 19 KB, sta in `guest/overlay-vendor/` ed e' **tracciato in git**. L'init lo copia
in `/vo/upper/lib64/hw/`, nella stessa cartella e con lo stesso meccanismo di
`hwcomposer.drm.so`.

Il **costruttore** dell'initramfs si FERMA se il file manca. Non e' pignoleria: senza, il
guest si avvia, suona, e ha l'anello di serie -- nessun messaggio, e cio' che si sente e'
esattamente lo stutter che quel lavoro serviva a togliere. L'**init** invece prosegue e lo
scrive su kmsg, perche' un guest che suona male e' meglio di uno che non parte.

### La prova, e perche' discrimina

`dumpsys media.audio_flinger` stampa `HAL frame count`:

| | |
|---|---|
| `vendor.img` vergine, nessun overlay | **1024** |
| stesso vendor + i 19 KB dall'initramfs | **4096** |

«L'audio si sente» non avrebbe provato niente: si sente anche con l'HAL di serie. Questo
numero cambia solo se la sostituzione ha funzionato davvero. Verificato anche che il file sia
nel guest (`/vendor/lib64/hw/audio.primary.waydroid.so`, 19.664 byte) e che l'md5 di
`vendor.img` sia tornato quello della riserva.

### Limiti dichiarati

- **Il `.so` e' legato alla build del vendor** da cui e' stato ricavato. Se l'immagine a
  monte si aggiorna, va ri-derivato: sovrapporre un HAL costruito per un'altra build rompe
  l'audio in silenzio.
- **Si redistribuiscono 19 KB** di un binario derivato da Waydroid. E' una scelta: un HAL
  audio AOSP e' Apache-2.0, che la redistribuzione di un binario modificato la permette con
  l'attribuzione -- al contrario delle GAPPS, proprietarie, ed e' il motivo per cui quelle si
  scaricano.
- **Il `.so` non e' ricostruibile dai sorgenti di questo repository**: e' estratto
  dall'immagine e committato com'e': rifarlo da zero significa ripetere quella
  estrazione a mano.
- **SELinux**: allarga di un file il debito gia' dichiarato per `hwcomposer.drm.so`.

### Un buco trovato per strada, e non e' di questo lavoro

**`hwcomposer.drm.so` (206 KB) NON e' tracciato in git**: sta in `guest/images/`, che e'
ignorata per intero. L'initramfs dipende quindi da un binario che esiste solo sulla macchina
di sviluppo, e **nessun altro puo' ricostruirlo**. Va risolto prima della release, insieme
alla domanda gemella: quali altri artefatti necessari stanno fuori dal repository.

> **Risposto dal censimento** (vedi la sezione sulla release piu' sotto). Il binario
> resta fuori da git, ma **non e' irrecuperabile**: `guest/scripts/build-drm-hwcomposer.sh`
> lo ricostruisce, e lo stesso vale per kernel, APK degli appunti, QEMU, guscio e initramfs.
> Costa un ambiente AOSP e una toolchain per il kernel -- e' un costo, non un vicolo cieco.
> Nel pacchetto di rilascio la domanda comunque non si pone: viaggia l'initramfs gia'
> costruito, con il compositore dentro.

## La release: un archivio che uno sconosciuto sa installare

`Habumi-0.1.0.zip`, **221.425.727 byte, 231 file**. Si scompatta dove si vuole,
si lancia `runtime\bin\Habumi.exe`, e al primo avvio il prodotto **scarica da se'** le
immagini di Android che gli mancano. Niente da installare, niente registro di Windows,
nessun privilegio di amministratore; si disinstalla cancellando una cartella.



### Perche' 0.1.0 e non 1.0.0

Il prodotto funziona ed e' provato, ma **non e' mai stato installato da nessuno tranne chi lo
sviluppa**, gira su una macchina sola, e porta limiti dichiarati: niente firma del codice,
nessun aggiornamento automatico, licenze delle DLL di MSYS2 da completare. Uno 0.x invita a
segnalare i difetti; un 1.0 promette una stabilita' che nessuno ha ancora potuto smentire.
La versione sta in **un posto solo**, in cima a `confeziona-release.sh`, con questa ragione
scritta accanto.

### Cosa viaggia e cosa si scarica

| viaggia | perche' |
|---|---|
| `kernel-guest-arm64`, `initramfs.img` | sono nostri e non esistono a monte |
| `qemu-nostro.exe` + DLL e `share/` **sfoltiti** | nostro (QEMU + `winq`), e le sue dipendenze |
| `Habumi.exe`, `adb.exe` e le due DLL | il guscio e gli strumenti |
| `empty-data.zip`, `config.txt`, `mappe/generico.txt` | i dati di partenza |
| `README.txt` (in CRLF), `LICENSES/` | la GPL di QEMU non e' facoltativa |

**Non viaggiano**: `system.img` (728 MB di zip) e `vendor.img` (77 MB). Sono vergini
entrambe -- il vendor lo e' tornato lo stesso giorno -- quindi **il progetto non
redistribuisce nessuna immagine del guest**, che era il primo vincolo di qualunque piano di
rilascio. Il kernel invece viaggia benche' sia la voce piu' grossa dopo QEMU: scaricarlo
metterebbe il primo avvio in dipendenza da un server nostro, e 44 MB non lo valgono.

Lo sfoltimento ha tolto **303 MB**, tutti di firmware UEFI per architetture che questo
prodotto non emula. L'elenco di cio' che entra e' un file tracciato,
`app/release/contenuto.txt`: il copione legge quello e **non decide niente da se'**, cosi'
il pacchetto e' riproducibile e la sua composizione si rivede in un diff. La ragione di ogni
taglio sta accanto alla voce, dentro quell'elenco.

### Il confezionamento

```bash
MSYSTEM=CLANGARM64 /c/msys64/usr/bin/bash.exe -lc "bash <radice>/app/scripts/confeziona-release.sh"
```

Il copione **costruisce** guscio e initramfs invece di raccogliere quello che trova (un
`Habumi.exe` raccolto a caso potrebbe essere quello di ieri, con un bug gia' corretto
nei sorgenti), si **FERMA** nominando il file quando l'elenco chiede qualcosa che non c'e', e
alla fine **rilegge il conto dei file dall'archivio prodotto**, non dall'albero composto:
stampare il numero che si voleva ottenere sarebbe mentire proprio nella riga che qualcuno
legge per credere che il pacchetto sia completo.

L'initramfs si costruisce nella **WSL**, che su questa macchina e' aarch64 davvero, mentre il
sottosistema POSIX di MSYS2 risponde `x86_64` anche sotto CLANGARM64.

### Il primo avvio, e i due buchi che ha smascherato

La prova che conta non e' «si avvia»: e' **scompattare in una cartella nuova, senza nessuna
immagine, e fare il giro da zero**. Fatta, ha trovato due difetti che nell'albero di
sviluppo erano invisibili -- perche' li' quei file c'erano da sempre.

**Nessuno scaricava la `system.img`.** Il prodotto sapeva scaricare il vendor
(`vendor_prepara`) e crearsi il `/data` (`dati_crea_se_manca`), ma non l'immagine della
variante attiva: chi scompattava il pacchetto finiva su `vm_verifica_file`, che lo mandava a
ricostruirla con `build-guest-kernel.sh` e `build-all.ps1` -- istruzioni prive di senso per
chi ha appena aperto uno zip, e che pretendono un ambiente di sviluppo. Adesso
`variante_prepara_attiva` **chiama lo stesso thread** che il menu usa per l'altra variante,
invece di duplicarne il percorso, e l'attesa pompa i messaggi (`guscio_pompa_attesa`,
condivisa con il vendor) perche' 728 MB sono minuti e una finestra che non pompa risulta
«non risponde».

**`guest/logs/` non esisteva nel pacchetto**, e questo ne uccideva due:

- la `fopen` del registro falliva, e il prodotto girava **senza registro** -- proprio il file
  che si chiede a chi segnala un problema;
- `-serial file:guest/logs/sessione-viva.log` non si apriva, **QEMU usciva subito**, tre
  volte di fila, e l'avvio falliva del tutto.

Una cartella vuota non e' un file, quindi non poteva stare in `contenuto.txt`. La crea
`reg_crea_cartelle` in `registro_apri`, camminando il percorso pezzo per pezzo perche'
`CreateDirectoryA` non crea i genitori.

**E il verdetto era falso.** Dopo le tre riprove il guscio diceva «il kernel si e' inchiodato
ogni volta», mandando a cercare la corsa non deterministica nell'accensione delle vCPU sotto
WHPX. Il kernel non era mai partito. Adesso `vm_usciti_subito` distingue i due guasti:

```
3 tentativi falliti: QEMU e' uscito subito OGNI volta, quindi il kernel non e'
mai partito. La ragione e' nelle righe [qemu] qui sopra -- non e' il guasto non
deterministico dell'accensione, e riprovare non aiuta.
```

### Cosa e' provato per misura

| | |
|---|---|
| scaricamento da zero di vendor **e** system | 235 MB + 1,62 GB scesi, verificati ed estratti dal prodotto |
| `/data` creato da solo | 4 GB da `empty-data.zip` |
| avvio al launcher senza nessuna immagine nel pacchetto | `sys.boot_completed=1`, `QuickstepLauncher` in cima |
| l'HAL audio dei 19 KB aggancia anche dal pacchetto | `HAL frame count: 4096` |
| appunti, dma_heap, EGL, mappa dei tasti | `com.godziller.habumi.clipboard`, `/dev/dma_heap/system`, `mesa`, `generico.txt` |
| il copione e' riproducibile | due giri di fila, stesso elenco di file |
| il verdetto nuovo | riprodotta la condizione vera (`guest/logs` come file), registro alla mano |
| **tocco, rotazione, audio e appunti dal pacchetto** | provati **a mano** sul pacchetto scompattato: funzionano. E' l'unica prova che poteva giudicare le DLL sfoltite, perche' nessun copione le esercita |

### L'md5 del vendor non e' lo stesso di questa macchina, ed e' normale

Notato cancellando la cartella di prova: la `vendor.img` che il prodotto **scarica** non e'
byte per byte quella dell'albero di sviluppo.

| | byte | md5 |
|---|---|---|
| scaricata dal prodotto | 235.524.096 | `289b95d7...` |
| albero di sviluppo | 252.301.312 | `b8af3acc...` |

**E' la stessa build**, verificato leggendo `build.prop` dentro le due immagini con `debugfs`
(`ro.vendor.build.date.utc=1775198504`, stesso fingerprint, stesso `ro.vendor.build.id`).
Cambia il contenitore ext4 -- 240 MiB contro 224,6 MiB -- non il contenuto. Il controllo che
il prodotto fa sulla data di build stava quindi facendo esattamente il suo mestiere, e l'HAL
audio dei 19 KB e' sovrapposto alla build per cui e' stato costruito.

**Ma l'md5 `b8af3acc...` scritto in `PROMPT-SESSIONE-NUOVA.md` vale solo per la copia di
questa macchina.** Chi verifica un'installazione nuova ne trova un altro e conclude che
qualcosa non torna. La verifica che regge su qualunque installazione e' la **data di build**,
non l'md5 dell'immagine.

### Limiti dichiarati

- **Provato su una macchina sola.** Il pacchetto e' stato scompattato altrove sullo stesso
  computer, non su un'altra macchina: una DLL di sistema che qui c'e' e altrove no non
  verrebbe scoperta. E' il limite piu' pesante di questa release.
- **Suono udibile, tocco e rotazione dalla finestra** passano dalle DLL sfoltite e si
  giudicano a orecchio e a mano: non li prova nessun copione. Fatto sul pacchetto
  scompattato, tutto funzionante -- ma resta un limite, perche' e' una prova che nessuna
  regressione futura ripetera' da sola.
- **Windows 11 ARM64**: `tar.exe` e' una dipendenza da Windows 11, gia' dichiarata.
- **Il primo avvio vuole circa 800 MB di rete** e circa 8 GB di disco liberi.
- **Nessuna firma del codice**: SmartScreen avvisera'.
- **Nessun aggiornamento automatico.**
- **Le immagini sono fissate alla build 20260403.**

## Il controllo dell'hypervisor

QEMU gira con `-accel whpx`. Se sulla macchina la funzionalita' di Windows **«Piattaforma
hypervisor Windows»** non e' attiva, QEMU esce subito, tre volte di fila, e l'avvio fallisce.
Fino a oggi chi installava vedeva un errore di QEMU e doveva **indovinare** che il rimedio era
una funzionalita' di Windows: non c'era nessuna riga che la nominasse.

`app/guscio/hyperv.c` lo chiede a Windows prima di partire.

### Perche' `WHvGetCapability` e non QEMU

Lanciare `qemu-nostro.exe` con una macchina vuota e leggere l'errore proverebbe **esattamente**
cio' che poi accadra', ed e' una virtu' vera. Costa pero' un processo a ogni avvio e, soprattutto,
**deduce da una stringa** cio' che qui si chiede a una funzione documentata: il testo dei messaggi
di QEMU cambia fra le versioni, il codice della capability no.

Scartata anche `Get-WindowsOptionalFeature`: **pretende privilegi elevati**, e un prodotto che
promette di non chiedere l'amministratore non puo' usarla nemmeno per controllare.

La DLL si carica **a runtime** e non si collega: dove la funzionalita' non e' installata
`WinHvPlatform.dll` puo' non esistere, e un collegamento statico impedirebbe al programma di
partire del tutto — cioe' proprio nel caso che il modulo esiste per spiegare.

### Tre esiti, non due

| esito | cosa fa |
|---|---|
| `HYPERV_SI` | prosegue in silenzio |
| `HYPERV_NO` | **ferma**, mostra il messaggio, non scarica niente |
| `HYPERV_NON_SO` | scrive una riga e **prosegue** |

«Non sono riuscito a chiederlo» non e' «non c'e'». Se una versione di Windows che non conosciamo,
o un antivirus, impedisse la chiamata, fermare sarebbe peggio che provarci — la macchina potrebbe
funzionare benissimo, e il messaggio direbbe di accendere una funzionalita' gia' accesa.
`hyperv_deve_fermare` e' una funzione separata proprio per poter essere provata: e' l'unica parte
verificabile senza avere una macchina senza hypervisor, ed e' quella che decide se un computer si
avvia o no.

### Dove sta il controllo, e perche' li'

**Dopo `registro_apri()`**, cosi' la ragione finisce nel registro anche se l'utente chiude subito
la finestra del messaggio.

**Prima di tutto il resto** — prima del vendor, prima della variante attiva, prima di `vm_apri` —
e questo va oltre cio' che la spec chiedeva. La spec diceva «prima di `vm_apri`», ma al primo
avvio fra i due ci stanno **805 MB di scaricamento**: controllare dopo vorrebbe dire far scaricare
quasi un gigabyte a qualcuno per poi dirgli che il suo computer non puo' avviare l'emulatore.

Il messaggio **non offre di accendere la funzionalita'**: e' una modifica alle impostazioni di
sistema, e chi e' arrivato fin qui ha appena cliccato «Esegui comunque» su SmartScreen.
Chiedergli l'elevazione subito dopo e' il modo migliore per farlo desistere.

### Come si prova, e cosa resta non provato

`GUSCIO_HYPERV_FINTO=no` o `=nonso` forza l'esito. **Sta nel prodotto di proposito**, non dietro
un `#ifdef`: serve proprio sul binario che si spedisce, perche' e' li' che si vuole vedere cosa
vede l'utente. Risponde solo a due valori esatti e non fa nulla piu' che restituire un esito.

Misurato, tutti e tre i casi:

| | QEMU | registro |
|---|---|---|
| `=no` | **non parte** | `hypervisor ASSENTE: serve la funzionalita' HypervisorPlatform...` |
| `=nonso` | parte | `non sono riuscito a chiederlo a Windows...` |
| normale | parte | nessuna riga |

**LIMITE DICHIARATO: il percorso vero non e' mai stato percorso.** Su questa macchina
l'hypervisor c'e'; quella sopra e' una simulazione dell'esito, non una prova su un computer che
ne e' privo. `WHvGetCapability` potrebbe inoltre rispondere di si' dove QEMU fallisce comunque:
quel caso ricade sul verdetto delle riprove, che distingue «QEMU e' uscito subito» da «il kernel
si e' inchiodato» — una rete diversa e piu' grossolana, ma che c'e'.

### Una trappola trovata provando, che vale oltre questo modulo

La prima verifica **non funzionava e sembrava un difetto del codice**: la variabile impostata con
`$env:` prima di `Start-Process` non arrivava al processo. Non e' colpa del guscio — `Start-Process`
di PowerShell 5.1 non propaga l'ambiente in modo affidabile, e nemmeno `start` dentro un `.cmd`,
che usa ShellExecute. Serve `ProcessStartInfo` con `UseShellExecute = $false` e
`EnvironmentVariables`.

E il difetto vero era un altro ancora: il blocco era finito in `modo_config()`, il ramo di
`--config`, perche' `dpi_esito()` compare **due volte** in `main.c` e una sostituzione «la prima
occorrenza» aveva preso quella sbagliata. Il codice era giusto e stava nel posto sbagliato: se
invece di misurare avessi dedotto, avrei cercato l'errore dentro `hyperv.c`.

## : licenza, inglese, e un nome (Habumi)

Una giornata sola, quattro sottoprogetti, e il prodotto passa da «funziona sulla mia macchina»
a «qualcuno potrebbe forkarlo».

### A -- licenza e governance

**Senza un file `LICENSE` il sorgente pubblicato non e' open source**: valgono tutti i diritti
riservati e nessuno puo' legalmente creare un branch, qualunque sia la lingua dei commenti. Era
il vero bloccante, e costava un file.

GPLv2 su tutto, titolare `Godziller`, **senza CLA** -- il che rende la licenza non piu'
cambiabile, ed e' voluto. Piu' `README.md`, `CONTRIBUTING.md`, `NOTICE.md`, `TRADEMARK.md`, e
l'intestazione di licenza su 49 sorgenti (non 51: `winq-coord.c` e la sua prova non si toccano).

**Scritto anche cio' che la licenza NON puo' dare**, perche' e' controintuitivo: nessuno puo'
chiudere questo prodotto, perche' il cuore e' QEMU ed e' GPL. Lo scenario da cui ci si vorrebbe
difendere e' gia' impossibile, e la doppia licenza non ha mercato. Restano donazioni, marchio e
build firmate.

**La bonifica del nome dell'azienda**, che la spec rimandava e che la decisione dell'utente ha
riportato qui: 13 occorrenze in 8 file piu' due percorsi di cartelle. Il pacchetto Android
diventa `com.godziller.habumi.clipboard` e l'APK `HabumiClipboard.apk`.

Due misure hanno reso l'operazione fattibile in un colpo solo, e sono state fatte invece che
supposte: l'initramfs installa l'APK **per nome di file**, e il guscio non nomina mai il
pacchetto a tempo di esecuzione, perche' il ponte passa da una porta TCP.

### D -- l'attrito d'installazione

Il muro vero non era la lingua: era **la virtualizzazione**. `-accel whpx` era cablato senza
controllo, e chi non aveva la «Piattaforma hypervisor Windows» attiva vedeva un errore di QEMU
e doveva indovinare che il rimedio fosse una funzionalita' di Windows.

`app/guscio/hyperv.c` lo chiede a `WHvGetCapability` -- non a QEMU, che costerebbe un processo
a ogni avvio e dedurrebbe da una stringa. **Tre esiti e non due**: «non sono riuscito a
chiederlo» non e' «non c'e'», e solo `HYPERV_NO` ferma.

Il controllo sta **prima dello scaricamento**, non solo prima di `vm_apri`: al primo avvio fra i
due ci stanno 805 MB, e dirlo dopo vorrebbe dire far scaricare un giga a qualcuno per poi
comunicargli che il suo computer non puo' partire.

Piu' `Start.cmd` in radice (un `.lnk` e' escluso: incorpora il percorso assoluto della macchina
che lo crea) e la correzione di **1,9 GB -> 805 MB** nel testo del pacchetto, un numero
sbagliato di 2,4 volte in eccesso.

### B1 -- la struttura

`phase0/1/2/3` diventano **`research/ guest/ qemu/ app/`**, e con loro i nomi che l'utente vede:
`LEGGIMI.txt`->`README.txt`, `LICENZE/`->`LICENSES/`, `Avvia.cmd`->`Start.cmd`,
`runtime/mappe/`->`runtime/keymaps/`, `configurazione.txt`->`config.txt`,
`varianti.txt`->`variants.txt`, `data-vuoto.zip`->`empty-data.zip`. Infine
`AndroidRuntime.exe`->**`Habumi.exe`**.

**1.803 riferimenti**, sistemati in ondate ordinate per pericolosita' decrescente: prima i 112
nel codice (dove un percorso sbagliato impedisce l'avvio), poi copioni ed elenchi, infine i
documenti. Nessun compilatore puo' accorgersi che una stringa contiene un percorso sbagliato:
per questo la prova finale e' il giro completo dal pacchetto, non «compila».

### B2 -- la lingua

**479 stringhe** in inglese: 304 del guscio, 175 di `winq`. Piu' le 174 righe di `config.txt`
con le sue 13 chiavi, e il token `tocco`->`touch` del formato dei profili.

Il prefisso del registro passa da `[guscio]` a **`[shell]`**, accanto a `[qemu]` e `[guest]`.

Restano in italiano i commenti nel codice e i documenti di progetto -- il sottoprogetto C -- e
il `README.md` lo dichiara a chi arriva.

### Le tre cose che questa giornata ha insegnato, e che valgono oltre

**Una ricerca nei letterali non trova le stringhe che il registro assembla.** Quattro residui
sono stati trovati solo **leggendo la finestra** dopo l'avvio: `dpi_esito()` e
`variante_manca_testo()` compongono il loro testo altrove e arrivano al registro come `%s`.
Nessun setaccio poteva vederli.

**Un file dimenticato non lascia residui nei sorgenti che guardi: li lascia nel binario.**
`winq-input.c` non era nell'elenco del piano, ed e' saltato fuori cercando l'italiano dentro
`qemu-nostro.exe` dopo la prima ricostruzione.

**Un token di protocollo si cambia su entrambi i lati o su nessuno.** `tocco` non era solo il
formato dei profili: era anche la parola che il guscio manda sul tubo e che `winq` confronta.
Cambiarne uno solo avrebbe rotto il modo impara **senza nessun errore**.


## Le righe OMX: il rumore che non e' un guasto

Guardando il registro di un'installazione appena fatta, una riga su cinque diceva
sempre la stessa cosa:

```
hwservicemanager: Since android.hardware.media.omx@1.0::IOmxStore is not registered,
                  trying to start it as a lazy HAL
init: Could not find service hosting interface
      android.hardware.media.omx@1.0::IOmxStore
```

**431 volte in sette minuti.** Il 20% di tutto quello che il guest ha da dire.

### Chi lo fa e perche' non smette

La catena, misurata e non dedotta:

1. `mediaserver` (pid 365) chiama `getService()` su `android.hardware.media.omx@1.0::IOmxStore`.
2. `hwservicemanager` non ce l'ha registrato e chiede a `init` di avviarlo come *lazy HAL*,
   scrivendo `ctl.interface_start`.
3. `init` cerca chi ospita quell'interfaccia, non trova niente, e lo scrive sulla seriale.
4. `HidlServiceManagement`, dentro `mediaserver`, aspetta **un secondo** e riprova. Per sempre.

Il ciclo e' progettato cosi': `getService()` in HIDL e' bloccante e ritenta finche' il servizio
non compare. Nessuno lo interrompe perche' nessuno dichiara che quel servizio non esistera' mai.

### Perche' l'HAL non c'e', e perche' va bene

Non manca per errore nostro. In `vendor.img` di Waydroid `arm64_only` **non esiste nessun binario**
in `/vendor/bin/hw/` che offra OMX, e **nessun `.rc`** che lo avvii: l'immagine e' *solo-Codec2*,
che su Android 13 e' la strada raccomandata a monte. OMX e' l'interfaccia vecchia.

Le tre misure che dicono che e' innocuo:

| Cosa | Valore |
|---|---|
| CPU di `mediaserver` in questa attesa | 0,0% |
| CPU di `hwservicemanager` | 0,0% |
| Servizi media attivi | tutti e sei, compreso `media.swcodec` |
| `debug.stagefright.ccodec` | vuoto, cioe' Codec2 attivo di default |

I codec funzionano. Un'app che decodifica un video passa da Codec2 e non si accorge di niente.

### Perche' si filtra invece di risolvere

Alla radice ci sarebbero due strade, ed entrambe sono peggio del problema:

- **Aggiungere l'HAL OMX** alla vendor: significa costruire e installare un servizio di sistema
  che non serve a nessuno, per farlo trovare da chi lo cerca per abitudine.
- **Impedire a `mediaserver` di cercarlo**: significa mettere le mani dentro un servizio di
  sistema del guest, con una patch nostra, per un fenomeno che non ha **nessuna** conseguenza
  funzionale. La cura sarebbe piu' invasiva della malattia.

Quello che invece e' un problema vero e' il **registro**: uno strumento in cui una riga su cinque
non significa niente non e' uno strumento. Fra sei mesi qualcuno cerchera' li' dentro un guasto
vero, e lo trovera' sepolto.

### Il filtro, e perche' conta invece di nascondere

`vm_riga_omx()` in `app/guscio/vm.c` riconosce la riga sul **nome esatto del servizio** --
non sulla parola `omx`, cosi' un messaggio diverso che parli di OMX continua a comparire.
Le righe riconosciute non finiscono nel registro, ma:

- la **prima** stampa una spiegazione per esteso, cosi' chi legge sa che il fenomeno esiste;
- ogni **cento** successive stampa `OMX HAL lookups suppressed so far: N`.

Non e' un `grep -v`: e' un conteggio dichiarato. Chi legge il registro sa quante righe sono
state tolte e perche'.

**Il dettaglio che poteva rompere tutto, e non l'ha fatto:** le righe soppresse continuano a
contare per `righe++`. Quel conteggio e' quello che il guardiano dell'avvio usa (`VM_RIGHE_SANE`)
per capire se il guest sta parlando o e' inchiodato. Se il filtro le avesse tolte anche dal
conto, un guest sanissimo che ripete OMX sarebbe stato dichiarato bloccato.

### La prova, sull'installazione pulita

Copiato il binario nuovo in `Habumi-installazione-da-zero` e riavviato:

```
righe [guest] nel registro : 1553
di cui IOmxStore           : 0
[shell] the guest keeps looking for the OMX media HAL, which this vendor image
        does not ship (it is Codec2-only). Harmless: codecs work through Codec2.
        These lines are suppressed from here on and counted.
[shell] OMX HAL lookups suppressed so far: 101
[shell] OMX HAL lookups suppressed so far: 201
[shell] the kernel started (1385 serial lines)
[shell] Android is ready
```

L'avvio e' stato riconosciuto e `sys.boot_completed` vale 1: il guardiano non ha perso niente.
