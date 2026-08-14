# Tenta la build del fork winq-emu-virglrenderer (branch alpha10) con Venus
# su Windows ARM64. L'esito negativo e' informativo quanto quello positivo:
# in entrambi i casi si scrive virgl-build.json.
#
# Perche' il fork e non l'upstream: una misura ha accertato che upstream e'
# POSIX-only (nessun handle Win32 nel renderer Venus). Il fork aggiunge lo
# shim fd->HANDLE.
#
# Perche' MSYS2 CLANGARM64 e non MSVC: il fork si compila con toolchain
# MinGW (il README documenta UCRT64/x86_64). Su ARM64 l'equivalente nativo
# e' CLANGARM64: clang 22 con target aarch64-w64-windows-gnu. MSVC non ha
# mai compilato questo codice, e il target ARM64 non e' nemmeno installato
# nei Build Tools presenti su questa macchina.

param(
    [string]$SrcDir  = (Join-Path $PSScriptRoot '..\winq-virgl'),
    [string]$OutDir  = (Join-Path $PSScriptRoot '..\results'),
    [string]$MsysDir = 'C:\msys64',
    [switch]$Reconfigure
)

. (Join-Path $PSScriptRoot 'env-check.ps1')
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$result = [ordered]@{
    built        = $false
    arch         = "unknown"
    artifact     = $null
    venusEnabled = $false
    videoEnabled = $false
    vtestBuilt   = $false
    toolchain    = $null
    fork         = $null
    errorTail    = $null
    timestamp    = (Get-Date -Format 'o')
}

function Save-Result {
    $script:result | ConvertTo-Json -Depth 4 |
        Out-File (Join-Path $OutDir 'virgl-build.json') -Encoding utf8
}

$bash = Join-Path $MsysDir 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) {
    $result.errorTail = "MSYS2 assente in $MsysDir. Esegui: winget install MSYS2.MSYS2"
    Save-Result; $result.errorTail; exit 1
}

$SrcDir = (Resolve-Path $SrcDir).Path
if (-not (Test-Path (Join-Path $SrcDir 'meson.build'))) {
    $result.errorTail = "sorgenti non trovati in $SrcDir"
    Save-Result; $result.errorTail; exit 1
}

# Il ramo del fork fa parte dell'esito: una build riuscita su un ramo
# sbagliato non dimostra nulla.
$result.fork = @{
    remote = (git -C $SrcDir remote get-url origin)
    branch = (git -C $SrcDir branch --show-current)
    commit = (git -C $SrcDir rev-parse --short HEAD)
}

$env:MSYSTEM = 'CLANGARM64'
$env:CHERE_INVOKING = '1'

# La toolchain va registrata: se clang non fosse aarch64, l'artefatto non
# potrebbe esserlo, e la misura sarebbe da buttare.
$clang = Join-Path $MsysDir 'clangarm64\bin\clang.exe'
$result.toolchain = @{
    clang     = (& $bash -lc "clang --version | head -1")
    target    = (& $bash -lc "clang --version | sed -n 's/^Target: //p'")
    clangArch = (Get-BinaryArch $clang)
    meson     = (& $bash -lc "meson --version")
    ninja     = (& $bash -lc "ninja --version")
}
if ($result.toolchain.clangArch -ne 'ARM64') {
    $result.errorTail = "clang non e' ARM64 ($($result.toolchain.clangArch)): la build sarebbe emulata"
    Save-Result; $result.errorTail; exit 1
}

$msysSrc = (& $bash -lc "cygpath -u '$SrcDir'").Trim()
$log = Join-Path $OutDir 'virgl-build.log'
$msysLog = (& $bash -lc "cygpath -u '$log'").Trim()

$setupCmd = if ($Reconfigure) { 'meson setup --reconfigure' } else { 'meson setup' }

"=== meson setup ==="
& $bash -lc "cd '$msysSrc' && $setupCmd builddir -Dvenus=true -Dvideo=true -Dtests=false -Dbuildtype=release > '$msysLog' 2>&1; echo EXIT=`$?" |
    Tee-Object -Variable setupOut | Select-Object -Last 1
Get-Content $log -Tail 25

if (($setupOut -join "`n") -notmatch 'EXIT=0') {
    $result.errorTail = (Get-Content $log -Tail 40) -join "`n"
    Save-Result
    "FAIL: meson setup non riuscito. Log completo in $log"
    exit 1
}

# Cosa ha effettivamente abilitato meson: il flag passato non basta come
# prova, va letto dal riepilogo di configurazione.
$cfg = Get-Content $log -Raw
$result.venusEnabled = $cfg -match '(?im)^\s*venus\s*:\s*(true|YES)'
$result.videoEnabled = $cfg -match '(?im)^\s*video\s*:\s*(true|YES)'

"=== ninja ==="
& $bash -lc "cd '$msysSrc' && ninja -C builddir >> '$msysLog' 2>&1; echo EXIT=`$?" |
    Tee-Object -Variable buildOut | Select-Object -Last 1
Get-Content $log -Tail 25

if (($buildOut -join "`n") -notmatch 'EXIT=0') {
    $result.errorTail = (Get-Content $log -Tail 40) -join "`n"
    Save-Result
    "FAIL: compilazione non riuscita. Log completo in $log"
    exit 1
}

$dll = Get-ChildItem -Path (Join-Path $SrcDir 'builddir') -Recurse `
    -Include 'libvirglrenderer*.dll','virglrenderer*.dll' -ErrorAction SilentlyContinue |
    Select-Object -First 1

$srv = Get-ChildItem -Path (Join-Path $SrcDir 'builddir') -Recurse `
    -Filter 'virgl_test_server*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
$result.vtestBuilt = [bool]$srv

if (-not $dll) {
    $result.errorTail = "compilazione conclusa ma nessuna DLL virglrenderer prodotta"
    Save-Result
    "FAIL: nessun artefatto trovato"
    exit 1
}

$result.built    = $true
$result.artifact = $dll.FullName
$result.arch     = Get-BinaryArch $dll.FullName
Save-Result

"OK: $($dll.FullName)  [$($result.arch)]"
"venus: $($result.venusEnabled)  video: $($result.videoEnabled)  vtest: $($result.vtestBuilt)"
if ($result.arch -ne 'ARM64') { "ATTENZIONE: artefatto non ARM64"; exit 1 }
