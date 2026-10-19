# SPDX-License-Identifier: GPL-2.0-only
# Isolated contract/error tests; the real ext4 acceptance test is separate.
[CmdletBinding()]param([string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$module=Import-Module (Join-Path $repo 'app/release/Profile-Data.psm1') -Force -PassThru
$root=Join-Path ([Environment]::GetFolderPath('Desktop')) ('Habumi-Workspace/Builds/Checks/growth-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $root 'runtime/bin') -Force|Out-Null
New-Item -ItemType Directory -Path (Join-Path $root 'guest/images/android') -Force|Out-Null
$data=Join-Path $root 'guest/images/android/data-gapps.img'
$script:checks=0
function Check([bool]$Value,[string]$Message){if(-not $Value){throw "FAIL: $Message"};$script:checks++;Write-Host "PASS: $Message"}
function MustFail([scriptblock]$Action,[string]$Message){$failed=$false;try{& $Action|Out-Null}catch{$failed=$true};Check $failed $Message}
function PutNumber([byte[]]$Buffer,[int]$Offset,[uint32]$Value){[BitConverter]::GetBytes($Value).CopyTo($Buffer,$Offset)}
$super=New-Object byte[] 65536
PutNumber $super 1028 16;PutNumber $super 1048 2
[BitConverter]::GetBytes([uint16]0xef53).CopyTo($super,1080)
PutNumber $super 1116 60;PutNumber $super 1120 68;PutNumber $super 1124 65539
(0..15|ForEach-Object {[byte]($_+1)}).CopyTo($super,1128)
[IO.File]::WriteAllBytes($data,$super)
$before=& $module {param($p) Get-HabumiExt4 $p} $data
Check ($before.FilesystemBytes -eq 65536 -and $before.BlockSize -eq 4096) 'raw ext4 geometry decoded'
Check ($before.Incompat -eq 64) 'transient journal-recovery bit excluded from identity'
Check ($before.ReadOnlyCompat -eq 3 -and $before.OrphansPresent) 'transient orphan-present bit recorded separately from capabilities'
$changed=$before|Select-Object *;$changed.Bytes=4GB;$changed.FilesystemBytes=4GB
$changed.NeedsRecovery=$false;$changed.OrphansPresent=$false
& $module {param($a,$b) Assert-HabumiGrowthGeometry $a $b 4GB} $before $changed
Check $true 'same identity at requested size accepted'
foreach($property in @('UUID','Compat','Incompat','ReadOnlyCompat','BlockSize','FilesystemBytes','Bytes')){
    $bad=$changed|Select-Object *
    if($property -eq 'UUID'){$bad.UUID='changed'}else{$bad.$property++}
    MustFail {& $module {param($a,$b) Assert-HabumiGrowthGeometry $a $b 4GB} $before $bad} "changed $property rejected"
}
foreach($property in @('NeedsRecovery','OrphansPresent')){
    $bad=$changed|Select-Object *;$bad.$property=$true
    MustFail {& $module {param($a,$b) Assert-HabumiGrowthGeometry $a $b 4GB} $before $bad} "uncleared $property state rejected"
}
$hash=(Get-FileHash -LiteralPath $data).Hash
MustFail {Expand-HabumiData $root data-gapps.img 4} 'missing maintenance bundle refuses growth'
Check ((Get-FileHash -LiteralPath $data).Hash -eq $hash) 'missing tools leave original intact'
New-Item -ItemType Directory -Path (Join-Path $root 'guest/maintenance') -Force|Out-Null
$ramdisk=Join-Path $root 'guest/maintenance/maintenance-initramfs.img'
$qemu=Join-Path $root 'runtime/bin/qemu-nostro.exe';$kernel=Join-Path $root 'guest/images/kernel-guest-arm64'
foreach($path in @($ramdisk,$qemu,$kernel)){[IO.File]::WriteAllText($path,'tool-fixture')}
$receipt=@{Schema='habumi.maintenance-build.v1';Protocol=1;Architecture='aarch64';E2fsprogs='1.47.2';ImageSHA256=(Get-FileHash $ramdisk).Hash}
$receipt|ConvertTo-Json|Set-Content (Join-Path $root 'guest/maintenance/maintenance-build.json')
@{CriticalFiles=@(@{Path='runtime/bin/qemu-nostro.exe';SHA256=(Get-FileHash $qemu).Hash},@{Path='guest/images/kernel-guest-arm64';SHA256=(Get-FileHash $kernel).Hash})}|ConvertTo-Json|Set-Content (Join-Path $root 'release-profile.json')
$null=& $module {param($r) Get-HabumiMaintenanceTools $r} $root
Check $true 'matching packaged tool identities accepted'
foreach($path in @($ramdisk,$qemu,$kernel)){
    [IO.File]::WriteAllText($path,'changed')
    MustFail {& $module {param($r) Get-HabumiMaintenanceTools $r} $root} ('changed tool refused: '+[IO.Path]::GetFileName($path))
    [IO.File]::WriteAllText($path,'tool-fixture')
}
# Equal-size and shrink requests fail without launching a guest.
PutNumber $super 1028 1048576
$stream=[IO.File]::Open($data,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$stream.Write($super,0,$super.Length);$stream.SetLength(4GB)}finally{$stream.Dispose()}
MustFail {Expand-HabumiData $root data-gapps.img 4} 'equal-size request refused'
$stream=[IO.File]::OpenWrite($data);try{$stream.SetLength(8GB)}finally{$stream.Dispose()}
MustFail {Expand-HabumiData $root data-gapps.img 4} 'shrink request refused'
PutNumber $super 1028 16;[IO.File]::WriteAllBytes($data,$super)
$guard=[IO.File]::Open((Join-Path $root '.habumi-profile.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try{MustFail {Expand-HabumiData $root data-gapps.img 4} 'active profile refuses growth'}finally{$guard.Dispose()}
MustFail {Expand-HabumiData $root '../data.img' 4} 'escaping image name refused'
MustFail {Expand-HabumiData $root data-gapps.img 17} 'capacity beyond initial 16 GiB scope refused'
$super[1080]=0;[IO.File]::WriteAllBytes($data,$super)
MustFail {Expand-HabumiData $root data-gapps.img 4} 'non-ext4 data refused'
$super[1080]=0x53;PutNumber $super 1048 20;[IO.File]::WriteAllBytes($data,$super)
MustFail {& $module {param($p) Get-HabumiExt4 $p} $data} 'invalid ext4 block-size exponent refused'
PutNumber $super 1048 2;[IO.File]::WriteAllBytes($data,$super)
# Actual child-process launch verifies timeout, exit status, nonce and pipes.
& (Join-Path $MsysBin 'clang.exe') -std=c11 -Wall -Werror -O2 (Join-Path $PSScriptRoot 'test-maintenance-runner.c') -o (Join-Path $root 'runtime/bin/qemu-nostro.exe')
if($LASTEXITCODE){throw 'Maintenance runner probe compilation failed.'}
foreach($mode in @('ok','missing','badnonce','nonzero','timeout')){
    $id=[guid]::NewGuid().ToString('N')
    New-Item -ItemType Directory -Path (Join-Path $root ('maintenance/'+$id)) -Force|Out-Null
    [IO.File]::WriteAllText((Join-Path $root 'maintenance-probe-mode.txt'),$mode)
    $run={& $module {param($r,$id) Invoke-HabumiMaintenance $r $id 'guest/images/android/data-gapps.img' 1} $root $id}
    if($mode -eq 'ok'){& $run;Check $true 'child has no console and both output pipes drain without deadlock'}
    else{MustFail $run "runner rejects $mode result"}
    Check ((Get-FileHash -LiteralPath $data).Hash -eq $hash) "runner $mode never opens original data"
}
# The runner must only ever receive the staged image path.
MustFail {& $module {param($r) Invoke-HabumiMaintenance $r 'bad' 'guest/images/android/data-gapps.img' 1} $root} 'invalid transaction rejected before process creation'
# A failed guest after staging must not publish a marker or replace original.
& $module {
    function script:Get-HabumiMaintenanceTools([string]$Root){return @{Protocol=1}}
    function script:Invoke-HabumiMaintenance {throw 'Injected staged-guest failure'}
}
MustFail {Expand-HabumiData $root data-gapps.img 4} 'staged guest failure rejects publication'
Check ((Get-FileHash -LiteralPath $data).Hash -eq $hash) 'staged failure preserves original bytes'
Check (-not(Test-Path -LiteralPath (Join-Path $root 'storage-maintenance.lock'))) 'staged failure requires no live-data recovery'
Write-Host "Growth checks: $script:checks passed. Fixture: $root"
