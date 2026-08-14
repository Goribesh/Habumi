# Avvia Android in una finestra, staccato da qualunque strumento di diagnosi.
# SOSTITUITO DA runtime/bin/Habumi.exe (vedi app/GUSCIO.md).
# Questo script resta perche' e' il modo di avviare QEMU SENZA il guscio, che
# serve a due cose: isolare un difetto stabilendo se venga dal guscio, e
# verificare che il percorso senza guscio continui a funzionare -- winq ricade
# su qmp_quit quando non trova l'evento di chiusura, e quel ramo va esercitato.
# Attenzione: qui la chiusura della finestra e' DURA, su un data.img montato
# cache=writeback. Per l'uso normale usare il guscio.
#
# Perche' esiste. guest_console.py e' fatto per pilotare un avvio e verificarne i
# traguardi: quando il suo scenario finisce, lui esce, e con lui muore QEMU. Per
# una sessione da usare -- guardare, toccare, provare app -- serve il contrario: un
# processo che vive per conto suo e che nessuno chiude.
#
# La seriale va su file invece che su socket, cosi' il log resta consultabile senza
# che nessuno debba stare in ascolto.
#
# Niente caratteri fuori dall'ASCII: PowerShell 5.1 legge i .ps1 senza BOM come
# ANSI e le lettere accentate rompono il parser.

param(
    [string]$Log = "guest\logs\sessione-viva.log",
    # Accende il monitor di QEMU, utile per iniettare eventi di input a mano e
    # distinguere un guasto di SDL da uno del percorso virtio.
    # Quale dispositivo di puntamento dare al guest. La scelta non e' cosmetica:
    #
    #   mouse       virtio-mouse-pci, coordinate RELATIVE. Android lo classifica
    #               come mouse (CursorInputMapper) e muove un cursore vero. E' la
    #               configurazione che ha piu' probabilita' di funzionare subito.
    #               In cambio SDL cattura il puntatore: si libera con Ctrl-Alt-G.
    #   multitouch  virtio-multitouch-pci, eventi ABS_MT_* con BTN_TOUCH reale.
    #               Android lo vede come schermo tattile: nessun cursore, tocco e
    #               trascinamento diretti.
    #   tablet      virtio-tablet-pci, coordinate ASSOLUTE ma senza BTN_TOUCH.
    #               MISURATO come non funzionante: Android riceve il movimento e non
    #               lo traduce, perche' ogni mapper adatto a un dispositivo assoluto
    #               pretende BTN_TOUCH. Restano solo i clic, fermi dove il cursore
    #               e' nato. Lasciato per poter rifare la misura.
    [ValidateSet("mouse", "multitouch", "tablet")]
    # DEFAULT CAMBIATO A multitouch dopo una verifica.
    #
    # Era "mouse" da quando il tocco non funzionava: con SDL il percorso
    # multitouch non veniva mai chiamato, quindi il mouse era l'unica scelta che
    # dava qualcosa. Ora -display winq consegna ABS_MT_* e BTN_TOUCH corretti, e
    # il mouse e' la scelta SBAGLIATA: virtio-mouse dichiara INPUT_EVENT_MASK_BTN
    # ma non MASK_MTT, quindi qemu_input_find_handler (ui/input.c:101-122) gli
    # consegna i BTN e SCARTA tutti gli eventi mtt -- nessun destinatario.
    #
    # Il sintomo e' fra i piu' ingannevoli incontrati in questo progetto: nel
    # guest arrivano i BTN_TOUCH ma NON le posizioni, quindi Android sa che un
    # dito e' appoggiato e non sa dove. Il tocco "non fa nulla" mentre ogni
    # verifica sul codice del backend risulta corretta. Misurato: 398 eventi
    # consegnati a virtio-mouse-device, zero al multitouch che non esisteva.
    # "auto" e non un dispositivo fisso, e la ragione e' un bug che ho appena
    # introdotto e che la review ha trovato: il dispositivo giusto DIPENDE dal
    # backend di display.
    #
    # -display winq -> multitouch. winq emette eventi MTT, e
    #                     virtio-multitouch e' l'unico che li accetta.
    #   -display sdl   -> mouse.      SDL non emette MAI eventi MTT (e' la
    #                     premessa dell'intero lavoro su host-window): emette REL.
    #
    # Fissare multitouch per tutti rompe sdl IN SILENZIO: virtio_multitouch_handler
    # dichiara INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_MTT e NON REL/ABS
    # (hw/input/virtio-input-hid.c:449-451), quindi i REL di SDL non trovano
    # destinatario e qemu_input_find_handler (ui/input.c:101-122) li scarta senza
    # dire nulla. Sintomo: lanciando lo script senza argomenti -- il caso
    # quotidiano -- il mouse non si muove piu' sotto sdl, e nessun messaggio lo
    # spiega. E' lo stesso scarto silenzioso che aveva tenuto nascosto il difetto
    # opposto: mouse con winq, dove gli MTT venivano buttati.
    [ValidateSet("auto", "mouse", "multitouch", "tablet")]
    [string]$Puntatore = "auto",
    [switch]$Monitor,
    [int]$MonitorPort = 55555,
    # Quale QEMU lanciare. "auto" prende il NOSTRO (qemu-nostro.exe) se c'e',
    # quello di pacman altrimenti. Il nostro e' ora il prodotto: e' l'unico che
    # conosce -display winq. Quello di pacman resta il termine di confronto A/B
    # e si chiede esplicitamente.
    [string]$Eseguibile = "auto",
    # Quale backend -display usare. "auto" da' winq col nostro binario, sdl con
    # quello di pacman -- che winq non lo conosce e uscirebbe subito.
    #
    # winq e' ora il default perche' e' l'unico che consegna l'INPUT: con sdl il
    # tocco non arriva al guest (misurato: 864 ABS_X, 952 ABS_Y e ZERO BTN_TOUCH
    # su 1058 eventi EV_KEY), ed e' il motivo per cui qemu/ui-winq esiste.
    #
    # Il compromesso, dichiarato: con winq il guest ottiene OpenGL ES 2.0 invece
    # di 3.1, perche' virglrenderer non ottiene contesti GL desktop da ANGLE e
    # ripiega sul proprio percorso EGL (dettagli in qemu/ui-winq/winq-present.c
    # righe 182-200 e in qemu/HOST-WINDOW.md). Con sdl il guest ottiene
    # D3D12 + ES 3.1 ma non si puo' toccare. Per la catena GL completa:
    #     -Eseguibile runtime/bin/qemu-system-aarch64.exe -Display sdl
    # che resta il confronto A/B preteso dal piano.
    [ValidateSet("auto", "sdl", "winq")]
    [string]$Display = "auto"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

# Il server adb puo' tenere occupata la porta inoltrata e QEMU rifiuta di partire con
# "Could not set up host forwarding rule". Va chiuso prima.
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
# ATTENZIONE al modo in cui si chiama. `adb kill-server` scrive su stderr quando il
# demone non e' in esecuzione, e PowerShell 5.1 con $ErrorActionPreference = "Stop"
# tratta l'output su stderr di un eseguibile nativo come errore TERMINANTE: lo
# script moriva qui, prima di lanciare QEMU, e la finestra si apriva e si chiudeva
# istantaneamente senza spiegare nulla. La redirezione 2>$null non basta, serve
# neutralizzare la preferenza attorno alla chiamata.
if (Test-Path $adb) {
    $vecchia = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $adb kill-server 2>&1 | Out-Null
    $ErrorActionPreference = $vecchia
}

Get-Process qemu-system-aarch64 -ErrorAction SilentlyContinue | ForEach-Object {
    Stop-Process -Id $_.Id -Force
}
Start-Sleep -Seconds 3

# La porta si verifica prima di usarla: una porta occupata fa uscire QEMU
# all'avvio, e il sintomo -- finestra vuota che sparisce -- non nomina la causa.
$monitorArg = "none"
if ($Monitor) {
    $occupata = (Get-NetTCPConnection -LocalPort $MonitorPort -ErrorAction SilentlyContinue) -ne $null
    if ($occupata) {
        Write-Output "porta $MonitorPort occupata: monitor non attivato"
    } else {
        $monitorArg = "telnet:127.0.0.1:$MonitorPort,server=on,wait=off"
        Write-Output "monitor QEMU su telnet 127.0.0.1 $MonitorPort"
    }
}

# L'eseguibile e il backend si scelgono insieme: winq esiste solo nel nostro
# binario, quindi chiederlo a quello di pacman lo fa uscire subito con
# "Parameter 'type' does not accept value 'winq'" -- un errore che non spiega che
# basta cambiare eseguibile.
if ($Eseguibile -eq "auto") {
    $Eseguibile = if (Test-Path "runtime/bin/qemu-nostro.exe") {
        "runtime/bin/qemu-nostro.exe"
    } else {
        "runtime/bin/qemu-system-aarch64.exe"
    }
}
# SI CHIEDE AL BINARIO, non al suo nome. La prima versione di questo blocco
# faceva `$Eseguibile -match 'qemu-nostro'`, e la review finale ha trovato che
# rendeva il branch inutilizzabile: assemble_runtime.py copia l'eseguibile
# mantenendo il nome qemu-system-aarch64.exe, e NESSUNO script del repo produce
# mai un file chiamato qemu-nostro.exe -- quel nome esisteva solo nelle copie a
# mano fatte durante lo sviluppo.
#
# Su un runtime assemblato dagli script il riconoscimento per nome falliva, e il
# sintomo era il peggiore possibile: -Display auto ripiegava su sdl e -Puntatore
# auto su mouse, cioe' IN SILENZIO la configurazione in cui il tocco non arriva
# al guest -- che e' precisamente cio' che qemu/ui-winq esiste per risolvere.
# E -Display winq esplicito veniva RIFIUTATO con un messaggio che accusava il
# binario sbagliato, mandando a ricompilare QEMU per niente.
#
# "-display help" e' la domanda giusta perche' la risposta viene dal binario
# stesso: elenca i backend che quel file conosce davvero, qualunque sia il suo
# nome e da dove venga.
#
# La preferenza va neutralizzata attorno alla chiamata: un eseguibile nativo che
# scrive su stderr con $ErrorActionPreference = "Stop" fa morire lo script, ed e'
# lo stesso inciampo gia' documentato piu' sopra per "adb kill-server".
$haWinq = $false
if (Test-Path $Eseguibile) {
    $vecchia = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $backend = & $Eseguibile -display help 2>&1 | Out-String
    $ErrorActionPreference = $vecchia
    $haWinq = $backend -match '(?m)^\s*winq\s*$'
} else {
    Write-Output "FERMO: $Eseguibile non esiste."
    exit 1
}

if ($Display -eq "auto") {
    $Display = if ($haWinq) { "winq" } else { "sdl" }
}
if (($Display -eq "winq") -and (-not $haWinq)) {
    Write-Output "FERMO: $Eseguibile non conosce -display winq."
    Write-Output "       I backend che dichiara sono:"
    ($backend -split "`n" | Where-Object { $_ -match '^\S' } | ForEach-Object { "         $_" })
    Write-Output "       Serve un QEMU con qemu/ui-winq innestato:"
    Write-Output "         bash qemu/scripts/build-qemu.sh"
    Write-Output "         bash qemu/scripts/innesta-winq.sh"
    Write-Output "         python guest/scripts/assemble_runtime.py --bridge <ponte> --out runtime"
    Write-Output "       Oppure lancia con -Display sdl."
    exit 1
}
Write-Output "eseguibile: $Eseguibile"
Write-Output "display: $Display"

# Il dispositivo di puntamento si accorda al backend: vedi il commento su
# $Puntatore. Con "auto" si sceglie, altrimenti si rispetta cio' che ha chiesto
# chi lancia -- anche una combinazione sbagliata, perche' serve a misurarla.
$puntatoreArg = if ($Puntatore -ne "auto") {
    $Puntatore
} elseif ($Display -eq "winq") {
    "multitouch"
} else {
    "mouse"
}
Write-Output "puntatore: $puntatoreArg (display $Display)"

$img = "guest/images/android"

# "gl=on" serve anche a winq, MISURATO non previsto: "-device virtio-gpu-gl-pci"
# e' sulla riga di comando a prescindere dal backend (e' cosi' che si resta
# confrontabili con sdl), e quel device si rifiuta di fare realize senza il
# flag globale display_opengl acceso, letto in
# hw/display/virtio-gpu-gl.c:127 ("The display backend does not have OpenGL
# support enabled"). winq lo alza nel proprio .early_init (winq-window.c),
# stesso punto e stesso pattern di ui/sdl2.c:845-853: NON vuol dire che winq
# disegni gia' qualcosa -- la finestra resta vuota di proposito, la
# presentazione e' il quella verifica -- vuol dire solo che il device puo' partire.
$displayArg = if ($Display -eq "winq") { "winq,gl=on" } else { "sdl,gl=on" }

# Gli argomenti come UNA sola stringa, non come array.
#
# Start-Process con -ArgumentList array unisce gli elementi con spazi senza citare
# quelli che li contengono: QEMU riceveva "printk.devkmsg=on" come nome di file.
# Citare a mano dentro l'elemento e' peggio, perche' le virgolette finiscono nel
# valore e il kernel legge
#     Kernel command line: "console=ttyAMA0 audit=0"
# cioe' non configura la console seriale e ignora tutti gli androidboot.*: il guest
# sembra inchiodarsi e la finestra resta su "Display output is not active".
#
# Con una stringa unica il quoting lo decide questo script e il runtime C di QEMU
# lo interpreta come atteso.
$argomenti = @(
    '-M virt -accel whpx -cpu host -m 6144 -smp 6',
    "-kernel guest/images/kernel-guest-arm64",
    "-initrd $img/initramfs.img",
    '-append "console=ttyAMA0 printk.devkmsg=on androidboot.hardware=waydroid androidboot.selinux=permissive audit=0"',
    "-drive file=$img/system.img,format=raw,if=virtio,readonly=on",
    "-drive file=$img/vendor.img,format=raw,if=virtio,readonly=on",
    "-drive file=$img/data.img,format=raw,if=virtio,cache=writeback,aio=threads",
    "-device virtio-gpu-gl-pci,venus=on,blob=on,hostmem=256M",
    "-device virtio-keyboard-pci",
    # Uno solo, di proposito: con due dispositivi di puntamento Android li fonde in
    # un'unica voce logica, perche' i device virtio non portano identificatori
    # distinti, e si ritrova due mapper sullo stesso dispositivo. Il sintomo
    # osservato e' un cursore che si muove ma clic che cadono al centro.
    "-device virtio-$puntatoreArg-pci",
    # LA PORTA DELL'HOST E' 15555, NON 5555, E LA RAGIONE E' adb.
    #
    # adb esplora le porte 5554-5585 in coppie -- console sulla pari, protocollo
    # adb sulla dispari -- per scoprire gli emulatori. Con l'inoltro sulla 5555
    # (dentro quell'intervallo) adb la prende per un emulatore, inventa da se'
    # un'entrata "emulator-5554", tenta la stretta di mano della CONSOLE sulla
    # 5554, non trova nulla, e lascia OFFLINE sia quella entrata sia la nostra.
    #
    # MISURATO: "adb devices" elencava
    #     127.0.0.1:5555   offline
    #     emulator-5554    offline
    # mentre nel guest adbd girava regolarmente (la seriale mostra il socket
    # creato e il servizio avviato). Il guasto e' INTERMITTENTE, perche' quello
    # scanner dipende dai tempi: ha funzionato per ore e poi ha smesso, e nessun
    # riavvio della VM lo rimetteva a posto.
    #
    # La porta del GUEST resta 5555, che e' quella su cui adbd ascolta
    # (service.adb.tcp.port in guest/scripts/android-vendor.props): cambia solo
    # il lato host. Da qui in avanti si usa
    #     adb connect 127.0.0.1:15555
    "-netdev user,id=n0,hostfwd=tcp:127.0.0.1:15555-:5555",
    "-device virtio-net-pci,netdev=n0",
    "-display $displayArg",
    "-serial file:$Log",
    "-monitor $monitorArg"
) -join ' '

$env:WINQ_DEVICE_EXCLUDE = "Direct3D12"
$env:WINQ_DIAG = "1"

# Lo stderr su file: con Start-Process andrebbe perso, e un QEMU che esce subito
# non lascerebbe alcuna spiegazione.
$errFile = Join-Path $root "guest\logs\qemu-stderr.log"

# Il kernel del guest si inchioda a volte all'inizializzazione di RCU, con la
# seriale ferma a [0.000000] e QEMU vivo: e' una corsa nell'accensione delle vCPU
# secondarie sotto WHPX. Non e' deterministico -- lo stesso comando a volte avvia in
# quaranta secondi -- quindi si rileva e si riprova, invece di lasciare all'utente
# una finestra bloccata su "Display output is not active".
function Avvia {
    if (Test-Path $Log) { Remove-Item $Log -Force -ErrorAction SilentlyContinue }
    $proc = Start-Process -FilePath $Eseguibile `
                          -ArgumentList $argomenti -PassThru `
                          -RedirectStandardError $errFile
    return $proc
}

$tentativi = 3
for ($t = 1; $t -le $tentativi; $t++) {
    $p = Avvia
    Write-Output "tentativo ${t}: QEMU pid $($p.Id)"

    # Sedici secondi bastano: un avvio sano supera le trecento righe di seriale
    # entro dieci, uno inchiodato resta sotto le cinquanta.
    Start-Sleep -Seconds 16
    $righe = 0
    if (Test-Path $Log) {
        $righe = (Get-Content $Log -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
    }

    if ($p.HasExited) {
        Write-Output "  QEMU e' uscito. Errore:"
        if (Test-Path $errFile) { Get-Content $errFile | Select-Object -First 6 }
        continue
    }
    if ($righe -lt 200) {
        Write-Output "  avvio inchiodato ($righe righe di seriale): chiudo e riprovo"
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 3
        continue
    }

    Write-Output "  avvio sano: $righe righe di seriale"
    for ($i = 1; $i -le 12; $i++) {
        Start-Sleep -Seconds 10
        if ($p.HasExited) { Write-Output "QEMU e' uscito durante l'avvio."; exit 1 }
        $fatto = Select-String -Path $Log -Pattern 'sys.boot_completed=1' -SimpleMatch -ErrorAction SilentlyContinue
        if ($fatto) {
            Write-Output ""
            Write-Output "AVVIO COMPLETATO. La finestra e' interattiva."
            Write-Output "  mouse e tastiera: direttamente nella finestra"
            Write-Output "  per guardare dentro: adb connect 127.0.0.1:15555"
            Write-Output ""
            Write-Output "Lascia aperto questo terminale: chiudendolo QEMU resta comunque vivo,"
            Write-Output "ma e' il modo piu' semplice per ritrovarlo."
            exit 0
        }
    }
    Write-Output "avviato ma nessun sys.boot_completed entro due minuti. Ultime righe:"
    Get-Content $Log -Tail 5 -ErrorAction SilentlyContinue
    exit 0
}
Write-Output "Tre tentativi falliti: il kernel si e' inchiodato ogni volta."
