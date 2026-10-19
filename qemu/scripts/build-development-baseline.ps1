# SPDX-License-Identifier: GPL-2.0-only
# Prepara e compila sorgenti verificati senza usare il vecchio albero QEMU.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$QemuSourceArchive,
      [Parameter(Mandatory)][string]$OutputRoot,
      [string]$MsysRoot='C:/msys64', [ValidateRange(1,32)][int]$Jobs=4,
      [switch]$PrepareOnly)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$lockPath=Join-Path $repo 'app/release/development-baseline.json'
$lock=Get-Content -LiteralPath $lockPath -Raw|ConvertFrom-Json
$archive=(Resolve-Path -LiteralPath $QemuSourceArchive).Path
if((Get-FileHash -LiteralPath $archive).Hash -ne $lock.QemuSource.SHA256){throw 'QEMU source archive differs from the pinned source offer.'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $OutputRoot){throw 'Output exists; choose a new empty build location.'}
$source=Join-Path $OutputRoot 'source'
$build=Join-Path $OutputRoot 'build'
New-Item -ItemType Directory -Path $source -Force|Out-Null
Copy-Item -LiteralPath $lockPath -Destination (Join-Path $OutputRoot 'baseline-lock.json')
& "$env:SystemRoot/System32/tar.exe" -xf $archive -C $source
if($LASTEXITCODE){throw 'Pinned QEMU source extraction failed.'}
if(([IO.File]::ReadAllText((Join-Path $source 'VERSION'))).Trim() -ne $lock.QemuVersion){throw 'Wrong QEMU source version.'}
$inputs=@()
foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $repo 'qemu/ui-winq') -File|Where-Object {$_.Name -like 'winq*' -and $_.Extension -in '.c','.h'})){
    $to=Join-Path $source ('ui/'+$file.Name)
    [IO.File]::WriteAllText($to,[IO.File]::ReadAllText($file.FullName).Replace("`r`n","`n"),[Text.UTF8Encoding]::new($false))
    $inputs+=@{Path=('qemu/ui-winq/'+$file.Name);SHA256=(Get-FileHash -LiteralPath $to).Hash}
}
if(@($inputs|Where-Object Path -like '*.c').Count -ne 9){throw 'Unexpected WinQ module count.'}
$commit=(& git -C $repo rev-parse HEAD).Trim()
$manifest=[ordered]@{SchemaVersion=1;SourceCommit=$commit;QemuVersion=$lock.QemuVersion;
    SourceMode='Working-tree snapshot; OverlayInputs hashes identify the compiled source.';
    SourceDirty=[bool]@(& git -C $repo status --porcelain --untracked-files=normal -- qemu/ui-winq qemu/scripts/build-development-baseline.ps1 qemu/scripts/build-development-baseline.sh app/release/development-baseline.json);
    QemuSourceArchiveSHA256=$lock.QemuSource.SHA256;OverlayInputs=$inputs;
    LockSHA256=(Get-FileHash -LiteralPath $lockPath).Hash;
    RecipeSHA256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'build-development-baseline.sh')).Hash;
    OriginalQemuTreeUsed=$false;CachedObjectsUsed=$false;State='prepared'}
$manifestPath=Join-Path $OutputRoot 'build-inputs.json'
$manifest|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifestPath -Encoding utf8
if($PrepareOnly){Write-Output "Prepared pinned source: $source";return}
$savedPath=$env:PATH;$savedSystem=$env:MSYSTEM
try{
    $env:MSYSTEM='CLANGARM64';$env:PATH="$MsysRoot/clangarm64/bin;$MsysRoot/usr/bin;$savedPath"
    & "$MsysRoot/usr/bin/bash.exe" --noprofile --norc (Join-Path $PSScriptRoot 'build-development-baseline.sh') $source $build $Jobs *> (Join-Path $OutputRoot 'build.log')
    if($LASTEXITCODE){Get-Content -LiteralPath (Join-Path $OutputRoot 'build.log') -Tail 35;throw 'Clean isolated QEMU build failed; log retained.'}
    $exe=Join-Path $build 'qemu-system-aarch64.exe'
    $manifest.State='built';$manifest.QemuSHA256=(Get-FileHash -LiteralPath $exe).Hash
    $manifest.Compiler=(& "$MsysRoot/clangarm64/bin/clang.exe" --version|Select-Object -First 1)
    $manifest.ToolchainPackages=@(& "$MsysRoot/usr/bin/pacman.exe" -Q|Where-Object {$_ -match '^mingw-w64-clang-aarch64-(clang|lld|meson|ninja|glib2|libepoxy|virglrenderer|SDL2|pixman|zstd|libslirp|libssh|libnfs|libusb) '})
    $manifest|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifestPath -Encoding utf8
    Write-Output "PASS: clean QEMU $($lock.QemuVersion) build; SHA256=$($manifest.QemuSHA256)"
}finally{$env:PATH=$savedPath;$env:MSYSTEM=$savedSystem}
