# Costruisce da zero tutto il necessario per eseguire l'emulatore, e lascia
# in runtime/ una directory autonoma.
#
# Idempotente: rieseguirlo su un albero gia' pronto non ricostruisce nulla
# inutilmente e non riapplica le patch.
#
# Cosa NON fa: non installa MSYS2 e non tocca l'installazione MSYS2. Se la
# toolchain manca, si ferma dicendo quale comando eseguire. La ragione e' che
# quei comandi richiedono elevazione e vanno visti dall'utente, non
# nascosti in uno script.

param(
    [string]$Root    = (Resolve-Path (Join-Path $PSScriptRoot '..\..')),
    [string]$MsysDir = 'C:\msys64',
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'

$fork       = Join-Path $Root 'research\winq-virgl'
$patches    = Join-Path $Root 'research\patches'
$runtime    = Join-Path $Root 'runtime'
$bridge     = Join-Path $fork 'builddir\src\libvirglrenderer-1.dll'
$forkUrl    = 'https://github.com/cmspam/winq-emu-virglrenderer.git'
$forkBranch = 'alpha10'

$bash = Join-Path $MsysDir 'usr\bin\bash.exe'
$clangBin = Join-Path $MsysDir 'clangarm64\bin'

function Step($msg) { Write-Host "`n=== $msg" -ForegroundColor Cyan }
function Die($msg)  { Write-Host "FERMO: $msg" -ForegroundColor Red; exit 1 }

# --- 1. toolchain ------------------------------------------------------------

Step 'Verifica della toolchain'

if (-not (Test-Path $bash)) {
    Die @"
MSYS2 assente in $MsysDir. Installalo con:

    winget install MSYS2.MSYS2
"@
}

$required = @{
    'clang.exe'                 = 'mingw-w64-clang-aarch64-clang'
    'meson'                     = 'mingw-w64-clang-aarch64-meson'
    'ninja.exe'                 = 'mingw-w64-clang-aarch64-ninja'
    'pkgconf.exe'               = 'mingw-w64-clang-aarch64-pkgconf'
    'objdump.exe'               = 'mingw-w64-clang-aarch64-binutils'
    'libepoxy-0.dll'            = 'mingw-w64-clang-aarch64-libepoxy'
    'vulkan-1.dll'              = 'mingw-w64-clang-aarch64-vulkan-loader'
    'libEGL.dll'                = 'mingw-w64-clang-aarch64-angleproject'
    'qemu-system-aarch64.exe'   = 'mingw-w64-clang-aarch64-qemu'
}

$missing = @()
foreach ($file in $required.Keys) {
    # meson e' uno script senza estensione: si cerca con e senza .exe
    $hit = @(Get-ChildItem -Path $clangBin -Filter "$file*" -ErrorAction SilentlyContinue)
    if (-not $hit) { $missing += $required[$file] }
}
if ($missing.Count -gt 0) {
    $pkgs = ($missing | Sort-Object -Unique) -join ' '
    Die @"
Pacchetti MSYS2 mancanti. Nella shell CLANGARM64:

    pacman -S --needed $pkgs
"@
}

$env:MSYSTEM = 'CLANGARM64'
$env:CHERE_INVOKING = '1'
$clangVer = (& $bash -lc "clang --version | head -1")
$clangTgt = (& $bash -lc "clang --version | sed -n 's/^Target: //p'")
"clang : $clangVer"
"target: $clangTgt"
if ($clangTgt -notmatch 'aarch64') {
    Die "clang non ha come target aarch64: la build sarebbe emulata."
}

# --- 2. sorgenti del ponte ---------------------------------------------------

Step 'Sorgenti del ponte grafico'

if (-not (Test-Path (Join-Path $fork 'meson.build'))) {
    "clono $forkUrl ramo $forkBranch"
    git clone --branch $forkBranch --depth 1 $forkUrl $fork
    if ($LASTEXITCODE -ne 0) { Die "clone non riuscito" }
} else {
    "gia' presente: $fork"
}

$forkCommit = (git -C $fork rev-parse --short HEAD)
$forkRef    = (git -C $fork branch --show-current)
"ramo $forkRef commit $forkCommit"

# --- 3. patch ---------------------------------------------------------------

Step 'Patch ai sorgenti di terze parti'

# Le patch non sono opzionali: senza la prima il codice non compila con
# clang 22, senza la seconda ogni percorso che dipende da THREAD_SYNC
# fallisce — e QEMU quel flag lo passa da se'.
foreach ($p in (Get-ChildItem $patches -Filter '*.patch' | Sort-Object Name)) {
    # --reverse --check riesce solo se la patch e' gia' applicata: e' il modo
    # di rendere idempotente il passo senza tenere uno stato a parte.
    git -C $fork apply --reverse --check $p.FullName 2>$null
    if ($LASTEXITCODE -eq 0) {
        "  $($p.Name): gia' applicata"
        continue
    }
    git -C $fork apply --check $p.FullName 2>$null
    if ($LASTEXITCODE -ne 0) {
        Die "$($p.Name) non si applica ne' risulta applicata. L'albero e' in uno stato inatteso."
    }
    git -C $fork apply $p.FullName
    if ($LASTEXITCODE -ne 0) { Die "applicazione di $($p.Name) non riuscita" }
    "  $($p.Name): applicata"
}

# --- 4. build del ponte -----------------------------------------------------

Step 'Build del ponte'

$buildDir = Join-Path $fork 'builddir'
if ($Rebuild -and (Test-Path $buildDir)) {
    "rimuovo $buildDir su richiesta (-Rebuild)"
    Remove-Item -Recurse -Force $buildDir
}

$uFork = (& $bash -lc "cygpath -u '$fork'").Trim()
if (-not (Test-Path (Join-Path $buildDir 'build.ninja'))) {
    & $bash -lc "cd '$uFork' && meson setup builddir -Dvenus=true -Dvideo=true -Dtests=false -Dbuildtype=release"
    if ($LASTEXITCODE -ne 0) { Die "meson setup non riuscito" }
}
& $bash -lc "cd '$uFork' && ninja -C builddir"
if ($LASTEXITCODE -ne 0) { Die "compilazione non riuscita" }

if (-not (Test-Path $bridge)) { Die "la build non ha prodotto $bridge" }

. (Join-Path $Root 'research\scripts\env-check.ps1')
$arch = Get-BinaryArch $bridge
"ponte : $bridge [$arch]"
if ($arch -ne 'ARM64') { Die "il ponte non e' ARM64 ($arch)" }

# --- 5. runtime autonomo ----------------------------------------------------

Step 'Assemblaggio del runtime'

python (Join-Path $PSScriptRoot 'assemble_runtime.py') `
    --msys $MsysDir --bridge $bridge --out $runtime
if ($LASTEXITCODE -ne 0) { Die "assemblaggio del runtime non riuscito" }

# --- 6. verifica ------------------------------------------------------------

Step 'Verifica del runtime'

$qemu = Join-Path $runtime 'bin\qemu-system-aarch64.exe'
$ver = (& $qemu --version | Select-Object -First 1)
"qemu  : $ver"

$accel = (& $qemu -accel help) -join ' '
if ($accel -notmatch 'whpx') { Die "questo qemu non ha l'acceleratore whpx" }
"accel : whpx presente"

$venus = (& $qemu -device virtio-gpu-gl-pci,help 2>&1) -join "`n"
if ($venus -notmatch 'venus=') { Die "virtio-gpu-gl-pci non espone venus=" }
"venus : opzione presente"

Write-Host "`nPRONTO." -ForegroundColor Green
@"
Runtime autonomo in: $runtime
  bin\qemu-system-aarch64.exe   qemu ARM64 nativo, whpx, venus
  bin\libvirglrenderer-1.dll    il nostro ponte, con Venus e lo shim Win32
  share\qemu\                   firmware e dati

Prova rapida (serve un'immagine guest ARM64):
  runtime\bin\qemu-system-aarch64.exe -M virt -accel whpx -cpu host -m 3072 -smp 4 ``
    -bios runtime\share\qemu\edk2-aarch64-code.fd ``
    -device virtio-gpu-gl-pci,venus=on,blob=on,hostmem=256M ``
    -display egl-headless -cdrom <immagine.iso> -serial stdio
"@
