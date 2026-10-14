# SPDX-License-Identifier: GPL-2.0-only
# Composizione esplicita: nessuna immagine Android usata o registro personale.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RuntimeRoot,
      [Parameter(Mandatory)][string]$RendererBuild,
      [Parameter(Mandatory)][string]$InitramfsPath,
      [Parameter(Mandatory)][string]$OutputRoot,
      [string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$RuntimeRoot=(Resolve-Path -LiteralPath $RuntimeRoot).Path
$RendererBuild=(Resolve-Path -LiteralPath $RendererBuild).Path
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $OutputRoot){throw 'Output already exists; select a new preparation directory.'}
$version=([IO.File]::ReadAllText((Join-Path $repoRoot 'app/release/version.txt'))).Trim()
if($version -notmatch '^\d+\.\d+\.\d+(-[a-z0-9.]+)?$'){throw 'Invalid release version.'}
$stage=Join-Path $OutputRoot 'package'
New-Item -ItemType Directory -Path $stage -Force|Out-Null
$paths=@(Get-Content (Join-Path $repoRoot 'app/release/contenuto-native.txt')|ForEach-Object {$_.Trim()}|Where-Object {$_ -and -not $_.StartsWith('#')})
foreach($relative in $paths){
    if($relative -match '(^|/)\.\.(/|$)' -or [IO.Path]::IsPathRooted($relative)){throw "Unsafe package path: $relative"}
    if($relative -match '(data[^/]*\.img|system[^/]*\.img|vendor\.img|guest/logs/|\.pre-)'){throw "Private/runtime state in allowlist: $relative"}
    $from=Join-Path $RuntimeRoot $relative
    if($relative -eq 'runtime/empty-data.zip' -or $relative -eq 'runtime/keymaps/generico.txt'){$from=Join-Path $repoRoot $relative}
    if($relative -eq 'runtime/bin/libvirglrenderer-1.dll'){$from=Join-Path $RendererBuild 'build/src/libvirglrenderer-1.dll'}
    if($relative -eq 'guest/images/android/initramfs.img'){$from=$InitramfsPath}
    $to=Join-Path $stage $relative
    New-Item -ItemType Directory -Path (Split-Path $to -Parent) -Force|Out-Null
    if($relative -notin @('runtime/bin/Habumi.exe','runtime/bin/config.txt')){Copy-Item -LiteralPath $from -Destination $to}
}
$source=Join-Path $repoRoot 'app/guscio'
$modules=@('main','finestra','vm','comandi','adb','config','registro','tubo','dpi','apk','file','rilascio','archivio','appunti','appunti-trama','varianti','dati','scarica','hyperv')
$files=@($modules|ForEach-Object {Join-Path $source "$_.c"})
& (Join-Path $MsysBin 'clang.exe') -std=c11 -Wall -Werror -O2 -mwindows -o (Join-Path $stage 'runtime/bin/Habumi.exe') @files -luser32 -lgdi32 -lcomctl32 -lshell32 -lws2_32 -lwinhttp -lbcrypt
if($LASTEXITCODE){throw 'Release shell compilation failed.'}
$encoding=[Text.UTF8Encoding]::new($false)
$config="# Habumi $version native Vulkan prerelease`nvcpu=6`nmemory=6144`nwidth=800`nheight=1280`nguest_scale=yes`nhz=120`nbattery_hz=60`ndensity=426`ngl=wgl`ngl_flush_wait=no`nretries=3`nadb_port=15555`nclipboard_port=15556`naudio=on`nvariant=gapps`n"
[IO.File]::WriteAllText((Join-Path $stage 'runtime/bin/config.txt'),$config,$encoding)
foreach($pair in @(@('Start-Vulkan.cmd','Start.cmd'),@('Start-Habumi.ps1','Start-Habumi.ps1'),@('README.txt','README.txt'))){
    $value=[IO.File]::ReadAllText((Join-Path $repoRoot ('app/release/'+$pair[0]))).Replace("`r`n","`n")
    if($pair[1] -eq 'Start.cmd' -or $pair[1] -eq 'README.txt'){$value=$value.Replace("`n","`r`n")}
    [IO.File]::WriteAllText((Join-Path $stage $pair[1]),$value,$encoding)
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'app/release/LICENSES') -Destination (Join-Path $stage 'LICENSES') -Recurse
$critical=@()
foreach($relative in @('runtime/bin/Habumi.exe','runtime/bin/qemu-nostro.exe','runtime/bin/libvirglrenderer-1.dll','runtime/bin/opengl32.dll','guest/images/kernel-guest-arm64','guest/images/android/initramfs.img','runtime/empty-data.zip')){
    $critical+=@{Path=$relative;SHA256=(Get-FileHash -LiteralPath (Join-Path $stage $relative)).Hash}
}
$manifest=@{Version=$version;Prerelease=$true;HostPowerExperimental=$true;CriticalFiles=$critical;
    Graphics='Qualcomm native Vulkan / Venus; Zink GLES composition';InitialDataGiB=4;
    UserDataIncluded=$false;RepeatedHibernateValidated=$false;
    ShaderBenchmarkOverheadGuarantee=$false;SourceRecipe='app/scripts/prepare-native-release.ps1'}
[IO.File]::WriteAllText((Join-Path $stage 'release-profile.json'),($manifest|ConvertTo-Json -Depth 6)+"`n",$encoding)
$inventory=@(Get-ChildItem -LiteralPath $stage -Recurse -File|ForEach-Object {
    @{Path=$_.FullName.Substring($stage.Length+1).Replace('\','/');Bytes=$_.Length;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
[IO.File]::WriteAllText((Join-Path $OutputRoot 'package-files.json'),($inventory|ConvertTo-Json -Depth 4)+"`n",$encoding)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=Join-Path $OutputRoot "Habumi-$version.zip"
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$archive,[IO.Compression.CompressionLevel]::Optimal,$false)
# Riapertura e confronto dei byte letti dall'archivio, non solo della directory.
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try{
    if($zip.Entries.Count -ne $inventory.Count){throw 'Archive entry count differs from package inventory.'}
    foreach($item in $inventory){
        $entry=$zip.GetEntry($item.Path)
        if(-not $entry -or $entry.Length -ne $item.Bytes){throw "Archive entry differs: $($item.Path)"}
        $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
        try{$hash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
        if($hash -ne $item.SHA256){throw "Archive hash differs: $($item.Path)"}
    }
}finally{$zip.Dispose()}
$archiveHash=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $OutputRoot 'SHA256SUMS.txt'),"$archiveHash  Habumi-$version.zip`n",$encoding)
Write-Output "PASS: $($inventory.Count) package entries verified; no installed Android data copied."
Get-FileHash -LiteralPath $archive
