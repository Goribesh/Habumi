# Scarica le build Mesa per Windows ARM64: dzn (Vulkan su D3D12) e
# d3d12 (OpenGL su D3D12). Fonte: mmozeiko/build-mesa, che pubblica
# gli asset arm64 per dispositivi Snapdragon X.

param(
    [string]$DestDir = (Join-Path $PSScriptRoot '..\mesa')
)

. (Join-Path $PSScriptRoot 'env-check.ps1')

New-Item -ItemType Directory -Force -Path $DestDir | Out-Null

$7z = Get-Command 7z.exe -ErrorAction SilentlyContinue
if (-not $7z) {
    "FAIL: 7z.exe non trovato. Installa 7-Zip, poi riesegui."
    "      winget install 7zip.7zip"
    exit 1
}

$api = 'https://api.github.com/repos/mmozeiko/build-mesa/releases/latest'
$rel = Invoke-RestMethod -Uri $api -Headers @{ 'User-Agent' = 'research' }
"Release trovata: $($rel.tag_name)"

# Un asset per driver: dzn per la catena Vulkan, d3d12 per quella OpenGL.
$wanted = @('dzn', 'd3d12')
$acquired = @{}

foreach ($drv in $wanted) {
    $asset = $rel.assets | Where-Object { $_.name -match "mesa-$drv-arm64" -and $_.name -match '\.7z$' } | Select-Object -First 1
    if (-not $asset) {
        "ATTENZIONE: nessun asset mesa-$drv-arm64 nella release $($rel.tag_name)"
        continue
    }

    $tmp = Join-Path $env:TEMP $asset.name
    "Scarico: $($asset.name)"
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tmp -UseBasicParsing

    $target = Join-Path $DestDir $drv
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    & $7z.Source x $tmp "-o$target" -y | Out-Null
    $acquired[$drv] = $target
    "  estratto in $target"
}

# Verifica dell'artefatto che conta per la catena Vulkan.
$dll = Get-ChildItem -Path $DestDir -Recurse -Filter 'vulkan_dzn.dll' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $dll) {
    "FAIL: vulkan_dzn.dll non presente negli archivi scaricati."
    "      Registra l'esito e vedi Step 3 del task."
    exit 1
}

$arch = Get-BinaryArch $dll.FullName
"vulkan_dzn.dll: $($dll.FullName)"
"architettura  : $arch"

# Solo ARM64 puro e' accettabile: ARM64EC e' ibrido e falserebbe le misure.
if ($arch -ne 'ARM64') {
    "FAIL: la DLL non e' ARM64 ($arch). Misurare con un binario non nativo non ha senso."
    exit 1
}

$icd = Get-ChildItem -Path $DestDir -Recurse -Filter '*dzn*icd*.json' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($icd) { "ICD manifest : $($icd.FullName)" } else { "ATTENZIONE: manifest ICD dzn non trovato" }

$gl = Get-ChildItem -Path $DestDir -Recurse -Filter 'opengl32.dll' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($gl) { "OpenGL d3d12 : $($gl.FullName) [$(Get-BinaryArch $gl.FullName)]" } else { "ATTENZIONE: fallback OpenGL d3d12 (opengl32.dll) non trovato" }

"OK: Mesa ARM64 pronta in $DestDir ($($acquired.Keys -join ', '))"
