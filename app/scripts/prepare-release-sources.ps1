# SPDX-License-Identifier: GPL-2.0-only
# Assemble source inputs explicitly; never collect an installation recursively.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PublishedSourceArchive,
      [Parameter(Mandatory)][string]$QemuSourceArchive,
      [Parameter(Mandatory)][string]$QemuBuildReceipt,
      [Parameter(Mandatory)][string]$MaintenanceBuildRoot,
      [Parameter(Mandatory)][string]$MaintenanceSourceRoot,
      [Parameter(Mandatory)][string]$RendererInputs,
      [Parameter(Mandatory)][string]$OutputRoot,
      [string]$ProjectCommit='HEAD')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$pin=Get-Content -LiteralPath (Join-Path $repo 'app/release/development-baseline.json') -Raw|ConvertFrom-Json
if((Get-FileHash -LiteralPath $PublishedSourceArchive).Hash -ne $pin.QemuSource.AssetSHA256){throw 'Published source archive differs from pin.'}
$renderer=Get-Content -LiteralPath $RendererInputs -Raw|ConvertFrom-Json
$qemu=Get-Content -LiteralPath $QemuBuildReceipt -Raw|ConvertFrom-Json
$maintenance=Get-Content -LiteralPath (Join-Path $MaintenanceBuildRoot 'maintenance-build.json') -Raw|ConvertFrom-Json
if($qemu.State -ne 'built' -or $qemu.OriginalQemuTreeUsed -or $qemu.CachedObjectsUsed -or @($qemu.OverlayInputs).Count -ne 19){throw 'Unexpected QEMU build receipt.'}
$commit=(& git -C $repo rev-parse --verify "$ProjectCommit^{commit}").Trim()
if($LASTEXITCODE){throw 'Project commit does not exist.'}
$version=(& git -C $repo show "${commit}:app/release/version.txt").Trim()
if($LASTEXITCODE -or $version -notmatch '^\d+\.\d+\.\d+(-[a-z0-9.]+)?$'){throw 'Invalid source version.'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $OutputRoot){throw 'Choose a new source output directory.'}
$stage=Join-Path $OutputRoot 'source-offer'
New-Item -ItemType Directory -Path $stage -Force|Out-Null
function Copy-Checked([string]$From,[string]$Relative,[string]$Hash){
    if([IO.Path]::IsPathRooted($Relative) -or $Relative.Contains('..')){throw 'Unsafe source output path.'}
    if((Get-FileHash -LiteralPath $From).Hash -ne $Hash){throw "Source hash differs: $Relative"}
    $target=Join-Path $stage $Relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force|Out-Null
    Copy-Item -LiteralPath $From -Destination $target
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$old=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($PublishedSourceArchive))
try{
    $reader=[IO.StreamReader]::new($old.GetEntry('COMPONENTS.json').Open())
    try{$components=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()}
    foreach($name in @('linux-6.18.35-patched.tar.gz','native-renderer-patched.tar.gz')){
        $entry=$old.GetEntry($name)
        if(-not $entry){throw "Missing published source: $name"}
        $inputStream=$entry.Open();$outputStream=[IO.File]::Open((Join-Path $stage $name),[IO.FileMode]::CreateNew)
        try{$inputStream.CopyTo($outputStream)}finally{$inputStream.Dispose();$outputStream.Dispose()}
        if((Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash -ne $components.SourceFiles.$name.SHA256){throw 'Published member hash differs.'}
    }
}finally{$old.Dispose()}
if((Get-FileHash -LiteralPath (Join-Path $stage 'native-renderer-patched.tar.gz')).Hash -ne $renderer.SourceMemberSHA256){throw 'Renderer source receipt differs.'}
Copy-Checked $QemuSourceArchive 'qemu-11.0.3-patched.tar.gz' (Get-FileHash -LiteralPath $QemuSourceArchive).Hash
$verify=Join-Path $OutputRoot 'qemu-overlay-verification';New-Item -ItemType Directory -Path $verify|Out-Null
$members=@($qemu.OverlayInputs|ForEach-Object {'./ui/'+[IO.Path]::GetFileName($_.Path)})
& "$env:SystemRoot/System32/tar.exe" -xf (Join-Path $stage 'qemu-11.0.3-patched.tar.gz') -C $verify @members
if($LASTEXITCODE){throw 'QEMU source verification extraction failed.'}
foreach($inputFile in $qemu.OverlayInputs){
    if((Get-FileHash -LiteralPath (Join-Path $verify ('ui/'+[IO.Path]::GetFileName($inputFile.Path)))).Hash -ne $inputFile.SHA256){throw 'QEMU overlay differs from compiled source.'}
}
Copy-Checked $QemuBuildReceipt 'qemu-build-inputs.json' (Get-FileHash -LiteralPath $QemuBuildReceipt).Hash
Copy-Checked $RendererInputs 'renderer-inputs.json' (Get-FileHash -LiteralPath $RendererInputs).Hash
Copy-Checked (Join-Path $MaintenanceBuildRoot 'e2fsprogs-1.47.2.tar.xz') 'maintenance/e2fsprogs-1.47.2.tar.xz' $maintenance.SourceSHA256
Copy-Checked (Join-Path $MaintenanceBuildRoot 'e2fsprogs-maintenance.patch') 'maintenance/e2fsprogs-maintenance.patch' $maintenance.SourcePatchSHA256
foreach($file in @('maintenance-build.json','e2fsprogs-NOTICE')){Copy-Checked (Join-Path $MaintenanceBuildRoot $file) ('maintenance/'+$file) (Get-FileHash -LiteralPath (Join-Path $MaintenanceBuildRoot $file)).Hash}
foreach($file in $maintenance.Inputs){Copy-Checked (Join-Path $repo ('guest/scripts/'+$file.Path)) ('maintenance/'+$file.Path) $file.SHA256}
$busybox=Get-Content -LiteralPath (Join-Path $MaintenanceSourceRoot 'busybox/source-receipt.json') -Raw|ConvertFrom-Json
if($busybox.ExpectedBinarySHA256 -ne $maintenance.BusyboxSHA256){throw 'BusyBox source receipt identifies a different binary.'}
foreach($file in $busybox.Files){Copy-Checked (Join-Path $MaintenanceSourceRoot ('busybox/'+$file.File)) ('maintenance/busybox/'+$file.File) $file.ExpectedSHA256}
Copy-Checked (Join-Path $MaintenanceSourceRoot ('busybox/'+$busybox.SourceManifest.File)) ('maintenance/busybox/'+$busybox.SourceManifest.File) $busybox.SourceManifest.ActualSHA256
Copy-Checked (Join-Path $MaintenanceSourceRoot 'busybox/source-receipt.json') 'maintenance/busybox/source-receipt.json' (Get-FileHash -LiteralPath (Join-Path $MaintenanceSourceRoot 'busybox/source-receipt.json')).Hash
$glibc=Get-Content -LiteralPath (Join-Path $MaintenanceSourceRoot 'glibc-2ubuntu2.3/source-receipt.json') -Raw|ConvertFrom-Json
if($glibc.SourceVersion -ne '2.43-2ubuntu2.3' -or -not $glibc.ExactSourceVariantMatch){throw 'Build-host glibc source identity is not established.'}
foreach($file in $glibc.Files){
    $from=[IO.Path]::GetFullPath((Join-Path $MaintenanceSourceRoot ('glibc-2ubuntu2.3/'+$file.LocalPath)))
    if(-not $from.StartsWith(([IO.Path]::GetFullPath($MaintenanceSourceRoot).TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'glibc source path escaped its input directory.'}
    $relative=if($file.LocalPath.StartsWith('../glibc-candidates/')){'maintenance/glibc-candidates/'+$file.File}else{'maintenance/glibc-2ubuntu2.3/'+$file.File}
    Copy-Checked $from $relative $file.SHA256
}
Copy-Checked (Join-Path $MaintenanceSourceRoot 'glibc-2ubuntu2.3/glibc_2.43-2ubuntu2.3.dsc') 'maintenance/glibc-2ubuntu2.3/glibc_2.43-2ubuntu2.3.dsc' $glibc.Manifest.SHA256
Copy-Checked (Join-Path $MaintenanceSourceRoot 'glibc-2ubuntu2.3/source-receipt.json') 'maintenance/glibc-2ubuntu2.3/source-receipt.json' (Get-FileHash -LiteralPath (Join-Path $MaintenanceSourceRoot 'glibc-2ubuntu2.3/source-receipt.json')).Hash
& git -C $repo archive --format=tar "--output=$(Join-Path $stage 'habumi-project-source.tar')" $commit
if($LASTEXITCODE){throw 'Project source archive failed.'}
$readme=@'
Habumi corresponding source

The project tar identifies the release commit. QEMU includes all 19 compiled
WinQ/protocol overlays; qemu-build-inputs.json identifies their hashes and tools.
Linux and the unchanged renderer are reused from the verified rc.1 source offer.
The renderer binary is reused, not rebuilt for this release.

Maintenance includes e2fsprogs 1.47.2 source/patch and the initramfs recipe,
Ubuntu BusyBox 1:1.37.0-7ubuntu1 source, and build-host glibc 2.43-2ubuntu2.3
source. Receipts identify downloads and observed build-host package metadata.
The shared glibc upstream archive lives under maintenance/glibc-candidates;
the selected Ubuntu patch and receipt live under maintenance/glibc-2ubuntu2.3.
Archive sizes/SHA256 were checked against source manifests; detached signatures
are supplied without claiming that their cryptographic signatures were verified.

See the project's docs/RELEASE-READINESS-0.3.0-rc.2.md for build recipes and
runtime limitations. Exact binary reproduction across toolchains/timestamps,
general hardware compatibility and a complete dependency audit are not claimed.
'@
[IO.File]::WriteAllText((Join-Path $stage 'README.txt'),$readme.Replace("`r`n","`n")+"`n",[Text.UTF8Encoding]::new($false))
$inventory=@(Get-ChildItem -LiteralPath $stage -Recurse -File|ForEach-Object {@{Path=$_.FullName.Substring($stage.Length+1).Replace('\','/');Bytes=$_.Length;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
$manifest=@{Version=$version;ProjectCommit=$commit;SourceFiles=$inventory;QemuOverlaysVerified=19;ArtifactState='prepared-and-verified'}
[IO.File]::WriteAllText((Join-Path $stage 'COMPONENTS.json'),($manifest|ConvertTo-Json -Depth 8)+"`n",[Text.UTF8Encoding]::new($false))
$archive=Join-Path $OutputRoot "Habumi-$version-corresponding-source.zip"
$zip=[IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
try{foreach($file in @(Get-ChildItem -LiteralPath $stage -Recurse -File)){
    $relative=$file.FullName.Substring($stage.Length+1).Replace('\','/')
    $level=if($file.Extension -in '.gz','.xz','.bz2'){[IO.Compression.CompressionLevel]::NoCompression}else{[IO.Compression.CompressionLevel]::Optimal}
    $null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$file.FullName,$relative,$level)
}}finally{$zip.Dispose()}
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try{
    if($zip.Entries.Count -ne $inventory.Count+1){throw 'Source entry count differs.'}
    foreach($file in $inventory){
        $entry=$zip.GetEntry($file.Path)
        if(-not $entry -or $entry.Length -ne $file.Bytes){throw 'Source archive size differs.'}
        $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
        try{$actual=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
        if($actual -ne $file.SHA256){throw 'Source archive content differs.'}
    }
}finally{$zip.Dispose()}
Write-Output "PASS: $($inventory.Count+1) source entries verified; 19 QEMU overlays match their compiled inputs."
Get-FileHash -LiteralPath $archive
