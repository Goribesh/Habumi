# SPDX-License-Identifier: GPL-2.0-only
# Focused, isolated tests for Profile-Data.psm1. Never targets a real profile.
[CmdletBinding()]
param([string]$ShellExecutable,[switch]$CrossProcessProbe,[string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$module=Join-Path $repo 'app/release/Profile-Data.psm1'
$checksRoot=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Habumi-Workspace/Builds/Checks'
New-Item -ItemType Directory -Path $checksRoot -Force|Out-Null
$root=Join-Path $checksRoot ('profile-data-'+[guid]::NewGuid().ToString('N'))
$fixture=Join-Path $root 'profile';$backup=Join-Path $root 'backup';$bad=Join-Path $root 'bad';$diag=Join-Path $root 'diagnostics'
New-Item -ItemType Directory -Path (Join-Path $fixture 'runtime/bin') -Force|Out-Null
New-Item -ItemType Directory -Path (Join-Path $fixture 'guest/images/android') -Force|Out-Null
New-Item -ItemType Directory -Path (Join-Path $fixture 'runtime/keymaps/nested') -Force|Out-Null
New-Item -ItemType Directory -Path (Join-Path $fixture 'guest/logs') -Force|Out-Null
function Put([string]$Path,[byte[]]$Bytes){[IO.File]::WriteAllBytes($Path,$Bytes)}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
function Check([bool]$Ok,[string]$Message){if(-not $Ok){throw "FAIL: $Message"};Write-Host "PASS: $Message"}
function MustFail([scriptblock]$Action,[string]$Message){$failed=$false;try{& $Action|Out-Null}catch{$failed=$true};Check $failed $Message}
$config=[Text.Encoding]::UTF8.GetBytes("# keep this comment`r`nUnknownKey=preserved`r`nProfile=`"fake`"`r`n")
$data1=[byte[]](0..255);$data2=[byte[]](255..0);$keymap=[Text.Encoding]::UTF8.GetBytes("keymap fixture`n")
Put (Join-Path $fixture 'runtime/bin/config.txt') $config
Put (Join-Path $fixture 'guest/images/android/data.img') $data1
Put (Join-Path $fixture 'guest/images/android/data-gapps.img') $data2
Put (Join-Path $fixture 'guest/images/android/vendor.img') $data1
Put (Join-Path $fixture 'runtime/keymaps/nested/test.qmap') $keymap
Put (Join-Path $fixture 'guest/logs/test.log') ([Text.Encoding]::UTF8.GetBytes('fixture log'))
Import-Module $module -Force
$sourcePaths=@('runtime/bin/config.txt','guest/images/android/data.img','guest/images/android/data-gapps.img','runtime/keymaps/nested/test.qmap')
$before=@{};foreach($p in $sourcePaths){$before[$p]=Hash (Join-Path $fixture $p)}
$null=Backup-HabumiProfile -ProfileRoot $fixture -Destination $backup
$manifest=Get-Content (Join-Path $backup 'backup.json') -Raw|ConvertFrom-Json
Check ($manifest.State -eq 'Complete' -and @($manifest.Files).Count -eq 4) 'backup captures config, both data images, and nested keymaps'
Check ((Hash (Join-Path $backup 'payload/runtime/bin/config.txt')) -eq $before['runtime/bin/config.txt']) 'backup preserves comments and unknown config keys byte-for-byte'
Check ((Hash (Join-Path $backup 'payload/guest/images/android/data.img')) -eq $before['guest/images/android/data.img'] -and (Hash (Join-Path $backup 'payload/guest/images/android/data-gapps.img')) -eq $before['guest/images/android/data-gapps.img']) 'backup preserves both fake data images byte-for-byte'
Check (@($manifest.Metadata.ReferenceFiles|Where-Object Path -eq 'guest/images/android/vendor.img').Count -eq 1) 'reference image identity is recorded without copying its payload'
foreach($p in $sourcePaths){Put (Join-Path $fixture $p) ([Text.Encoding]::UTF8.GetBytes('changed '+$p))}
$changed=@{};foreach($p in $sourcePaths){$changed[$p]=Hash (Join-Path $fixture $p)}
$restored=Restore-HabumiProfile -ProfileRoot $fixture -BackupPath $backup
Check ($restored.Phase -eq 'Committed') 'restore commits successfully'
foreach($p in $sourcePaths){Check ((Hash (Join-Path $fixture $p)) -eq $before[$p]) "restore reproduces $p exactly"}
foreach($p in $sourcePaths){Check ((Hash (Join-Path $restored.PreviousFiles $p)) -eq $changed[$p]) "restore retains pre-restore original $p"}
MustFail {Backup-HabumiProfile -ProfileRoot $fixture -Destination $backup} 'backup refuses an existing destination'
Put (Join-Path $fixture 'guest/images/android/vendor.img') $data2
MustFail {Restore-HabumiProfile -ProfileRoot $fixture -BackupPath $backup} 'mismatched reference image refuses restore'
Check ((Hash (Join-Path $fixture 'runtime/bin/config.txt')) -eq $before['runtime/bin/config.txt']) 'image mismatch leaves profile data unchanged'
Put (Join-Path $fixture 'guest/images/android/vendor.img') $data1
$null=Export-HabumiDiagnostics -ProfileRoot $fixture -Destination $diag
$diagFiles=@(Get-ChildItem $diag -Recurse -File|ForEach-Object {$_.FullName.Substring($diag.Length+1).Replace('\','/')})
Check (-not ($diagFiles -match 'data(-gapps)?\.img$')) 'diagnostics excludes data image payloads'
Check ((Get-Content (Join-Path $diag 'diagnostics.json') -Raw).Contains('data-gapps.img')) 'diagnostics may report image metadata without including image bytes'
# Corrupt payload, missing payload, traversal, duplicate paths, and incomplete manifests.
$cases=@('corrupt','missing','traversal','duplicate','incomplete')
foreach($case in $cases){
  $bad=Join-Path $root ('bad-'+$case)
  Copy-Item -LiteralPath $backup -Destination $bad -Recurse
  $mp=Join-Path $bad 'backup.json';$m=Get-Content $mp -Raw|ConvertFrom-Json
  switch($case){
    corrupt {Put (Join-Path $bad 'payload/runtime/bin/config.txt') ([Text.Encoding]::UTF8.GetBytes('tampered'))}
    missing {Remove-Item -LiteralPath (Join-Path $bad 'payload/runtime/bin/config.txt')}
    traversal {$m.Files[0].Path='../escape.txt';$m.Files[0].SHA256='0'*64}
    duplicate {$m.Files=@($m.Files)+$m.Files[0]}
    incomplete {$m.State='Incomplete'}
  }
  if($case -in @('traversal','duplicate','incomplete')){$m|ConvertTo-Json -Depth 16|Set-Content -LiteralPath $mp -Encoding utf8}
  MustFail {Restore-HabumiProfile -ProfileRoot $fixture -BackupPath $bad} "restore rejects $case backup"
  foreach($p in $sourcePaths){Check ((Hash (Join-Path $fixture $p)) -eq $before[$p]) "rejected $case backup leaves $p unchanged"}
}
# Lock via the same .NET no-sharing semantics used by the module.
$lockPath=Join-Path $fixture '.habumi-profile.lock';$held=[IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try{MustFail {Export-HabumiDiagnostics -ProfileRoot $fixture -Destination (Join-Path $root 'locked-diag')} 'exclusive profile lock rejects concurrent operation'}finally{$held.Dispose()}
if($CrossProcessProbe){
  $cc=Join-Path $MsysBin 'clang.exe';if(-not(Test-Path $cc)){throw "Missing clang: $cc"}
  $probe=Join-Path $fixture 'runtime/bin/profile-lock-probe.exe'
  & $cc -std=c11 -Wall -Werror -O2 (Join-Path $repo 'app/guscio/test-profilo-lock.c') '-o' $probe
  if($LASTEXITCODE -ne 0){throw "clang probe compile failed: $LASTEXITCODE"}
  $old=Get-Location;Push-Location $fixture
  try{$proc=Start-Process -FilePath $probe -PassThru -WorkingDirectory $fixture; $until=(Get-Date).AddSeconds(10);while(-not(Test-Path 'lock-ready.txt') -and (Get-Date) -lt $until){Start-Sleep -Milliseconds 50};Check (Test-Path 'lock-ready.txt') 'cross-process probe acquired fixture profile lock';MustFail {Export-HabumiDiagnostics -ProfileRoot $fixture -Destination (Join-Path $root 'cross-process-diag')} 'cross-process exclusive lock rejects concurrent operation';$proc.WaitForExit();Check ($proc.ExitCode -eq 0) 'cross-process lock probe exits cleanly'}finally{Pop-Location}
}
if($ShellExecutable){
  $shell=Join-Path $fixture 'runtime/bin/Habumi.exe';Copy-Item -LiteralPath $ShellExecutable -Destination $shell
  function RunStatus([string]$Name){$out=Join-Path $root ($Name+'.out');$err=Join-Path $root ($Name+'.err');$p=Start-Process -FilePath $shell -ArgumentList '--storage-status' -WorkingDirectory $fixture -PassThru -Wait -RedirectStandardOutput $out -RedirectStandardError $err;return $p.ExitCode}
  Check ((RunStatus 'available') -eq 0) 'shell reports storage available (exit 0)'
  Put (Join-Path $fixture 'storage-maintenance.lock') ([Text.Encoding]::UTF8.GetBytes('{"Schema":"habumi.maintenance.v1"}'))
  Check ((RunStatus 'pending') -eq 3) 'shell reports pending maintenance marker (exit 3)'
  Remove-Item (Join-Path $fixture 'storage-maintenance.lock')
  $held=[IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
  try{Check ((RunStatus 'locked') -eq 2) 'shell reports locked profile (exit 2)'}finally{$held.Dispose()}
}
Write-Host "Fixture: $root"
Copy-Item -LiteralPath (Join-Path $repo 'app/release/Manage-Data.ps1') -Destination (Join-Path $fixture 'Manage-Data.ps1')
Copy-Item -LiteralPath $module -Destination (Join-Path $fixture 'Profile-Data.psm1')
Copy-Item -LiteralPath (Join-Path $repo 'app/release/Profile-Growth.ps1') -Destination (Join-Path $fixture 'Profile-Growth.ps1')
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fixture 'Manage-Data.ps1') -Operation Backup -Path (Join-Path $root 'wrapper-backup')
Check ($LASTEXITCODE -eq 0) 'packaged wrapper resolves its own profile root without an explicit argument'
Check (Test-Path -LiteralPath (Join-Path $root 'wrapper-backup/backup.json')) 'packaged wrapper publishes a verified backup'
foreach($kind in @('parent-file','target-directory')){
  $target=Join-Path $root $kind
  New-Item -ItemType Directory -Path (Join-Path $target 'runtime/bin') -Force|Out-Null
  Put (Join-Path $target 'runtime/bin/config.txt') $config
  Put (Join-Path $target 'guest-placeholder.txt') $data1
  if($kind -eq 'parent-file'){
    New-Item -ItemType Directory -Path (Join-Path $target 'runtime/keymaps') -Force|Out-Null
    Put (Join-Path $target 'runtime/keymaps/nested') $data1
  }else{New-Item -ItemType Directory -Path (Join-Path $target 'runtime/keymaps/nested/test.qmap') -Force|Out-Null}
  MustFail {Restore-HabumiProfile -ProfileRoot $target -BackupPath $backup} "restore rejects $kind topology conflict"
  Check ((Hash (Join-Path $target 'runtime/bin/config.txt')) -eq $before['runtime/bin/config.txt']) 'topology conflict leaves configuration untouched'
}
