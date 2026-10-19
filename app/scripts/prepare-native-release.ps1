# SPDX-License-Identifier: GPL-2.0-only
# Composizione esplicita: nessuna immagine Android usata o registro personale.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RuntimeRoot,
      [string]$RendererBuild='',
      [Parameter(Mandatory)][string]$InitramfsPath,
      [Parameter(Mandatory)][string]$OutputRoot,
      [string]$MaintenanceBuildRoot='',
      [string]$MsysBin='C:/msys64/clangarm64/bin',
      [string]$QemuExecutable='', [string]$BuildProvenancePath='',
      [string]$ShellSourceDirectory='',
      [switch]$DevelopmentBaseline,
      [ValidateSet('A0','A1')][string]$DevelopmentName='A0',
      [ValidateRange(1024,65535)][int]$AdbPort=15555,
      [ValidateRange(1024,65535)][int]$ClipboardPort=15556)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$RuntimeRoot=(Resolve-Path -LiteralPath $RuntimeRoot).Path
if($RendererBuild){$RendererBuild=(Resolve-Path -LiteralPath $RendererBuild).Path}
if(-not $DevelopmentBaseline -and -not $RendererBuild){throw 'Normal release preparation requires an explicit renderer build.'}
if($ShellSourceDirectory -and -not $DevelopmentBaseline){throw 'A shell snapshot is accepted only for isolated development builds.'}
if($DevelopmentBaseline -and (-not $QemuExecutable -or -not $BuildProvenancePath)){throw 'Development builds require explicit QEMU and provenance inputs.'}
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
    if($relative -eq 'runtime/bin/libvirglrenderer-1.dll' -and $RendererBuild){$from=Join-Path $RendererBuild 'build/src/libvirglrenderer-1.dll'}
    if($relative -eq 'runtime/bin/qemu-nostro.exe' -and $QemuExecutable){$from=$QemuExecutable}
    if($relative -eq 'guest/images/android/initramfs.img'){$from=$InitramfsPath}
    $to=Join-Path $stage $relative
    New-Item -ItemType Directory -Path (Split-Path $to -Parent) -Force|Out-Null
    if($relative -notin @('runtime/bin/Habumi.exe','runtime/bin/config.txt')){Copy-Item -LiteralPath $from -Destination $to}
}
$source=Join-Path $repoRoot 'app/guscio'
if($ShellSourceDirectory){$source=(Resolve-Path -LiteralPath $ShellSourceDirectory).Path}
$modules=@('main','finestra','vm','comandi','adb','config','registro','tubo','dpi','apk','file','rilascio','archivio','appunti','appunti-trama','varianti','dati','scarica','hyperv','sessione')
$files=@($modules|ForEach-Object {Join-Path $source "$_.c"})
& (Join-Path $MsysBin 'clang.exe') -std=c11 -Wall -Werror -O2 -mwindows -o (Join-Path $stage 'runtime/bin/Habumi.exe') @files -luser32 -lgdi32 -lcomctl32 -lshell32 -lws2_32 -lwinhttp -lbcrypt -ladvapi32
if($LASTEXITCODE){throw 'Release shell compilation failed.'}
$launcherMode=if($DevelopmentBaseline -and $DevelopmentName -eq 'A1'){'1'}else{'0'}
& (Join-Path $MsysBin 'clang.exe') -std=c11 -Wall -Werror -O2 -mwindows -municode "-DHABUMI_CHILD_WINDOW=$launcherMode" -o (Join-Path $stage 'Habumi.exe') (Join-Path $repoRoot 'app/release/launcher.c') -luser32 -lshell32
if($LASTEXITCODE){throw 'Graphical launcher compilation failed.'}
$encoding=[Text.UTF8Encoding]::new($false)
$config="# Habumi $version native Vulkan prerelease`nvcpu=6`nmemory=6144`nwidth=800`nheight=1280`nguest_scale=yes`nhz=120`nbattery_hz=60`ndensity=426`ngl=wgl`ngl_flush_wait=no`nretries=3`nadb_port=$AdbPort`nclipboard_port=$ClipboardPort`naudio=on`nvariant=gapps`n"
[IO.File]::WriteAllText((Join-Path $stage 'runtime/bin/config.txt'),$config,$encoding)
foreach($pair in @(@('Start-Vulkan.cmd','Start.cmd'),@('Start-Habumi.ps1','Start-Habumi.ps1'),@('README.txt','README.txt'))){
    $value=[IO.File]::ReadAllText((Join-Path $repoRoot ('app/release/'+$pair[0]))).Replace("`r`n","`n")
    if($pair[1] -eq 'Start.cmd' -or $pair[1] -eq 'README.txt'){$value=$value.Replace("`n","`r`n")}
    [IO.File]::WriteAllText((Join-Path $stage $pair[1]),$value,$encoding)
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'app/release/LICENSES') -Destination (Join-Path $stage 'LICENSES') -Recurse
foreach($name in @('Manage-Data.ps1','Profile-Data.psm1','Profile-Growth.ps1')){Copy-Item -LiteralPath (Join-Path $repoRoot ('app/release/'+$name)) -Destination (Join-Path $stage $name)}
if($MaintenanceBuildRoot){
    $MaintenanceBuildRoot=(Resolve-Path -LiteralPath $MaintenanceBuildRoot).Path
    $maintenance=Get-Content -LiteralPath (Join-Path $MaintenanceBuildRoot 'maintenance-build.json') -Raw|ConvertFrom-Json
    if($maintenance.Schema -ne 'habumi.maintenance-build.v1' -or $maintenance.Protocol -ne 1 -or $maintenance.Architecture -ne 'aarch64' -or
       $maintenance.E2fsprogs -ne '1.47.2' -or $maintenance.SourceSHA256 -ne '08242e64ca0e8194d9c1caad49762b19209a06318199b63ce74ae4ef2d74e63c' -or
       (Get-FileHash -LiteralPath (Join-Path $MaintenanceBuildRoot 'maintenance-initramfs.img')).Hash -ne $maintenance.ImageSHA256 -or
       (Get-FileHash -LiteralPath (Join-Path $MaintenanceBuildRoot 'e2fsprogs-1.47.2.tar.xz')).Hash -ne $maintenance.SourceSHA256 -or
       (Get-FileHash -LiteralPath (Join-Path $MaintenanceBuildRoot 'e2fsprogs-maintenance.patch')).Hash -ne $maintenance.SourcePatchSHA256){throw 'Maintenance build identity check failed.'}
    foreach($name in @('build-maintenance-initramfs.sh','maintenance-init.sh')){
        $sourceInput=@($maintenance.Inputs|Where-Object Path -eq $name)
        if($sourceInput.Count -ne 1 -or (Get-FileHash -LiteralPath (Join-Path $repoRoot ('guest/scripts/'+$name))).Hash -ne $sourceInput[0].SHA256){throw "Maintenance source differs from build receipt: $name"}
    }
    $maintenanceTarget=Join-Path $stage 'guest/maintenance';New-Item -ItemType Directory -Path $maintenanceTarget|Out-Null
    foreach($name in @('maintenance-build.json','maintenance-initramfs.img')){Copy-Item -LiteralPath (Join-Path $MaintenanceBuildRoot $name) -Destination (Join-Path $maintenanceTarget $name)}
    $maintenanceLicenses=Join-Path $stage 'LICENSES/maintenance';New-Item -ItemType Directory -Path $maintenanceLicenses|Out-Null
    foreach($name in @('e2fsprogs-NOTICE','e2fsprogs-1.47.2.tar.xz','e2fsprogs-maintenance.patch')){Copy-Item -LiteralPath (Join-Path $MaintenanceBuildRoot $name) -Destination (Join-Path $maintenanceLicenses $name)}
    foreach($name in @('build-maintenance-initramfs.sh','maintenance-init.sh')){Copy-Item -LiteralPath (Join-Path $repoRoot ('guest/scripts/'+$name)) -Destination (Join-Path $maintenanceLicenses $name)}
}
$critical=@()
foreach($relative in @('Habumi.exe','Start-Habumi.ps1','Manage-Data.ps1','Profile-Data.psm1','Profile-Growth.ps1','runtime/bin/Habumi.exe','runtime/bin/qemu-nostro.exe','runtime/bin/libvirglrenderer-1.dll','runtime/bin/opengl32.dll','guest/images/kernel-guest-arm64','guest/images/android/initramfs.img','runtime/empty-data.zip')){
    $critical+=@{Path=$relative;SHA256=(Get-FileHash -LiteralPath (Join-Path $stage $relative)).Hash}
}
if($MaintenanceBuildRoot){foreach($relative in @('guest/maintenance/maintenance-build.json','guest/maintenance/maintenance-initramfs.img')){$critical+=@{Path=$relative;SHA256=(Get-FileHash -LiteralPath (Join-Path $stage $relative)).Hash}}}
$manifest=@{Version=$version;Prerelease=$true;HostPowerExperimental=$true;CriticalFiles=$critical;
    OfflineDataTools=$true;DataToolsSchema='habumi.backup.v1';
    OfflineDataGrowth=[bool]$MaintenanceBuildRoot;
    Graphics='Qualcomm native Vulkan / Venus; Zink GLES composition';InitialDataGiB=4;
    UserDataIncluded=$false;RepeatedHibernateValidated=$false;
    ShaderBenchmarkOverheadGuarantee=$false;SourceRecipe='app/scripts/prepare-native-release.ps1'}
if($DevelopmentBaseline){
    Copy-Item -LiteralPath $BuildProvenancePath -Destination (Join-Path $stage 'development-build.json')
    $manifest.CriticalFiles+=@{Path='development-build.json';SHA256=(Get-FileHash -LiteralPath (Join-Path $stage 'development-build.json')).Hash}
    $manifest.DevelopmentBuild=$DevelopmentName;$manifest.ExperimentalOmxProbe=$true
    $manifest.ExperimentalSystemUiNullProbe=$true;$manifest.ExperimentalRendererRotation=$true
    if($DevelopmentName -eq 'A1'){
        $manifest.ExperimentalChildWindow=$true
        $manifest.CompactInterface=$true
        $manifest.Interface='Native frame, aspect-fitted Android view and 36-DIP right command rail'
        $start="@echo off`r`nstart `"`" `"%~dp0Habumi.exe`"`r`n"
        [IO.File]::WriteAllText((Join-Path $stage 'Start-A1.cmd'),$start,$encoding)
    }
}
[IO.File]::WriteAllText((Join-Path $stage 'release-profile.json'),($manifest|ConvertTo-Json -Depth 6)+"`n",$encoding)
$inventory=@(Get-ChildItem -LiteralPath $stage -Recurse -File|ForEach-Object {
    @{Path=$_.FullName.Substring($stage.Length+1).Replace('\','/');Bytes=$_.Length;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
[IO.File]::WriteAllText((Join-Path $OutputRoot 'package-files.json'),($inventory|ConvertTo-Json -Depth 4)+"`n",$encoding)
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archiveName=if($DevelopmentBaseline){"Habumi-development-$DevelopmentName.zip"}else{"Habumi-$version.zip"}
$archive=Join-Path $OutputRoot $archiveName
# .NET Framework's CreateFromDirectory uses backslash entry names. Explicit
# portable names keep PS5 and PS7 packages identical in path semantics.
$zip=[IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
try{foreach($item in $inventory){$null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $stage $item.Path),$item.Path,[IO.Compression.CompressionLevel]::Optimal)}}finally{$zip.Dispose()}
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
[IO.File]::WriteAllText((Join-Path $OutputRoot 'SHA256SUMS.txt'),"$archiveHash  $archiveName`n",$encoding)
Write-Output "PASS: $($inventory.Count) package entries verified; no installed Android data copied."
Get-FileHash -LiteralPath $archive
