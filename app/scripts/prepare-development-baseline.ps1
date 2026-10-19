# SPDX-License-Identifier: GPL-2.0-only
# Compone la baseline da un archivio pubblico verificato e dalla build pulita.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RuntimeArchive,
      [Parameter(Mandatory)][string]$QemuBuildRoot,
      [Parameter(Mandatory)][string]$InitramfsPath,
      [Parameter(Mandatory)][string]$OutputRoot,
      [string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$lockPath=Join-Path $repo 'app/release/development-baseline.json'
$lock=Get-Content -LiteralPath $lockPath -Raw|ConvertFrom-Json
$archive=(Resolve-Path -LiteralPath $RuntimeArchive).Path
$initramfs=(Resolve-Path -LiteralPath $InitramfsPath).Path
if((Get-FileHash -LiteralPath $archive).Hash -ne $lock.RuntimeArchive.SHA256){throw 'Runtime archive differs from the pinned public release.'}
$expectedInit=@($lock.PinnedFiles|Where-Object Path -eq 'guest/images/android/initramfs.img')[0].SHA256
if((Get-FileHash -LiteralPath $initramfs).Hash -ne $expectedInit){throw 'Experimental initramfs differs from the recorded development baseline.'}
$QemuBuildRoot=(Resolve-Path -LiteralPath $QemuBuildRoot).Path
$build=Get-Content -LiteralPath (Join-Path $QemuBuildRoot 'build-inputs.json') -Raw|ConvertFrom-Json
$qemu=Join-Path $QemuBuildRoot 'build/qemu-system-aarch64.exe'
if($build.State -ne 'built' -or $build.OriginalQemuTreeUsed -or $build.CachedObjectsUsed -or
    $build.QemuSourceArchiveSHA256 -ne $lock.QemuSource.SHA256 -or
    (Get-FileHash -LiteralPath $qemu).Hash -ne $build.QemuSHA256){throw 'Clean QEMU build provenance does not match the executable.'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $OutputRoot){throw 'Output exists; select a new baseline directory.'}
New-Item -ItemType Directory -Path $OutputRoot|Out-Null
$runtime=Join-Path $OutputRoot 'pinned-runtime'
New-Item -ItemType Directory -Path $runtime|Out-Null
& "$env:SystemRoot/System32/tar.exe" -xf $archive -C $runtime
if($LASTEXITCODE){throw 'Public runtime extraction failed.'}
foreach($file in $lock.PinnedFiles){
    $path=if($file.Path -eq 'guest/images/android/initramfs.img'){$initramfs}else{Join-Path $runtime $file.Path}
    if((Get-FileHash -LiteralPath $path).Hash -ne $file.SHA256){throw "Pinned component differs: $($file.Path)"}
}
$snapshot=Join-Path $OutputRoot 'shell-source'
New-Item -ItemType Directory -Path $snapshot|Out-Null
$inputs=@()
foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $repo 'app/guscio') -File|Where-Object Extension -in '.c','.h')){
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $snapshot $file.Name)
    $inputs+=@{Path=('app/guscio/'+$file.Name);SHA256=(Get-FileHash -LiteralPath $file.FullName).Hash}
}
$provenance=[ordered]@{SchemaVersion=1;DevelopmentBuild='A0';Published=$false;
    SourceCommit=(& git -C $repo rev-parse HEAD).Trim();
    LockSHA256=(Get-FileHash -LiteralPath $lockPath).Hash;
    RuntimeArchiveSHA256=$lock.RuntimeArchive.SHA256;ComponentVersions=$lock.ComponentVersions;
    PinnedFiles=$lock.PinnedFiles;QemuBuild=$build;ShellInputs=$inputs;
    SourceMode='Working-tree snapshots; file hashes, not HEAD alone, identify the built source.';
    SourceDirty=[bool]@(& git -C $repo status --porcelain --untracked-files=normal -- app/guscio qemu/ui-winq app/scripts/prepare-development-baseline.ps1 qemu/scripts/build-development-baseline.ps1);
    ExperimentalOmxProbe=$true;ExperimentalSystemUiNullProbe=$true;
    ReproducibilityScope='Clean shell/QEMU build, pinned binary dependencies; not a bit-identical rebuild of every third-party component.'}
$provenancePath=Join-Path $OutputRoot 'development-build.json'
$provenance|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $provenancePath -Encoding utf8
& (Join-Path $PSScriptRoot 'prepare-native-release.ps1') -RuntimeRoot $runtime -InitramfsPath $initramfs -OutputRoot (Join-Path $OutputRoot 'distribution') -QemuExecutable $qemu -BuildProvenancePath $provenancePath -ShellSourceDirectory $snapshot -DevelopmentBaseline -AdbPort 17555 -ClipboardPort 15556 -MsysBin $MsysBin
if(-not(Test-Path -LiteralPath (Join-Path $OutputRoot 'distribution/SHA256SUMS.txt'))){throw 'Baseline package was not produced.'}
Write-Output 'PASS: isolated A0 baseline packaged from clean QEMU and pinned dependencies. No Android user disk was copied.'
