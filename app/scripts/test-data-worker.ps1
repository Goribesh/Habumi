# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]param([string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$base=Join-Path ([Environment]::GetFolderPath('Desktop')) ('Habumi-Workspace/Builds/Checks/data-ui-'+[guid]::NewGuid().ToString('N'))
$root=Join-Path $base ('profile space '+[char]0xe8)
New-Item -ItemType Directory -Path (Join-Path $root 'runtime/bin') -Force|Out-Null
New-Item -ItemType Directory -Path (Join-Path $root 'guest/images/android') -Force|Out-Null
foreach($name in @('Data-Worker.ps1','Profile-Data.psm1','Profile-Growth.ps1','Profile-SystemInfo.ps1')){Copy-Item -LiteralPath (Join-Path $repo ('app/release/'+$name)) -Destination (Join-Path $root $name)}
[IO.File]::WriteAllText((Join-Path $root 'runtime/bin/config.txt'),"# keep`nvariant=gapps`nvcpu=6`nprivate_test=secret-value`n")
$data=Join-Path $root 'guest/images/android/data-gapps.img';[IO.File]::WriteAllBytes($data,[byte[]](0..255))
$original=(Get-FileHash $data).Hash
$exe=Join-Path $base 'job-probe.exe'
& (Join-Path $MsysBin 'clang.exe') -std=c11 -Wall -Werror -O2 -municode (Join-Path $PSScriptRoot 'test-data-job.c') (Join-Path $repo 'app/release/data-job.c') -o $exe -lole32 -lshell32
if($LASTEXITCODE){throw 'Data job probe compilation failed'}
& $exe;if($LASTEXITCODE){throw 'Argument quoting tests failed'}
$script:checks=0
function Check([bool]$ok,[string]$message){if(-not $ok){throw "FAIL: $message"};$script:checks++;Write-Host "PASS: $message"}
function Run([string]$operation,[string]$path,[int]$expected=0){
    if(-not $path){$path='-'}
    $output=@(& $exe $root $operation $path 'data-gapps.img' 16)
    $code=$LASTEXITCODE
    if($code -ne $expected){Write-Host ($output|Out-String)}
    Check ($code -eq $expected) "$operation worker exit status $expected"
    $job=[string]$output[0]
    Check (Test-Path -LiteralPath (Join-Path $job 'request.json')) 'native request path preserves spaces and Unicode'
    return $job
}
$backup=Join-Path $base ('backup space '+[char]0xe8)
$job=Run Backup $backup
Check ((Get-FileHash (Join-Path $backup 'payload/guest/images/android/data-gapps.img')).Hash -eq $original) 'native worker publishes exact backup data'
$result=Get-Content (Join-Path $job 'result.json') -Raw|ConvertFrom-Json
Check ($result.State -eq 'Succeeded' -and $result.Operation -eq 'Backup') 'worker persists versioned successful outcome'
[IO.File]::WriteAllText($data,'modified')
$null=Run Restore $backup
Check ((Get-FileHash $data).Hash -eq $original) 'native worker restores original bytes'
$diag=Join-Path $base 'diagnostics'
$null=Run Diagnostics $diag
$info=Get-Content (Join-Path $diag 'diagnostics.json') -Raw|ConvertFrom-Json
Check ($info.Host.Configured.vcpu -eq '6') 'diagnostic bundle includes configured settings'
Check (($info.Host|ConvertTo-Json -Depth 8) -notlike '*secret-value*') 'unknown setting value is excluded'
Check (@(Get-ChildItem $diag -Recurse -File|Where-Object Name -like 'data*.img').Count -eq 0) 'diagnostic bundle excludes data images'
$oldModulePath=$env:PSModulePath
try{
    $env:PSModulePath=Join-Path $base 'missing-modules'
    $null=Run Backup (Join-Path $base 'native-module-path-backup')
}finally{$env:PSModulePath=$oldModulePath}
Check (Test-Path (Join-Path $base 'native-module-path-backup/backup.json')) 'GUI worker repairs an incompatible inherited module path'
$null=Run Grow '' 1
Check ((Get-FileHash $data).Hash -eq $original) 'invalid growth preserves original'
$guard=[IO.File]::Open((Join-Path $root '.habumi-profile.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try{$null=Run Backup (Join-Path $base 'busy-backup') 1}finally{$guard.Dispose()}
Check (-not(Test-Path (Join-Path $base 'busy-backup'))) 'busy native worker never publishes a backup'
# A pending marker still permits bounded local diagnostics; Recover refuses bad evidence.
[IO.File]::WriteAllText((Join-Path $root 'storage-maintenance.lock'),'{}')
$pending=Join-Path $base 'pending-diagnostics'
$null=Run Diagnostics $pending
Check ((Get-Content (Join-Path $pending 'diagnostics.json') -Raw|ConvertFrom-Json).RecoveryPending) 'pending recovery is represented in diagnostics'
$null=Run Recover '' 1
Check (Test-Path (Join-Path $root 'storage-maintenance.lock')) 'failed recovery keeps startup blocked'
# The native completion reader must reject stale nonce and missing status.
$fake=@'
param([string]$RequestPath)
$request=Get-Content -LiteralPath $RequestPath -Raw|ConvertFrom-Json
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class ConsoleCheck { [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow(); }'
$directory=[IO.Path]::GetDirectoryName($RequestPath)
[IO.File]::WriteAllText((Join-Path $directory 'console.txt'),[ConsoleCheck]::GetConsoleWindow().ToInt64())
[IO.File]::WriteAllText((Join-Path $directory 'status.txt'),"HABUMI-DATA-UI-V1 wrong`nSucceeded`nFake`n`n",[Text.Encoding]::Unicode)
exit 0
'@
[IO.File]::WriteAllText((Join-Path $root 'Data-Worker.ps1'),$fake)
$fakeJob=Run Backup (Join-Path $base 'fake-backup') 1
Check ((Get-Content (Join-Path $fakeJob 'console.txt') -Raw) -eq '0') 'native worker receives no console window'
Check (-not(Test-Path (Join-Path $base 'fake-backup'))) 'stale nonce cannot report success'
function Set-FakeWorker([string]$mode){
    $scriptText=@'
param([string]$RequestPath)
$request=Get-Content -LiteralPath $RequestPath -Raw|ConvertFrom-Json
$directory=[IO.Path]::GetDirectoryName($RequestPath)
if('__MODE__' -ne 'NoStatus'){
    $state=if('__MODE__' -eq 'WorkingZero'){'Working'}else{'Succeeded'}
    [IO.File]::WriteAllText((Join-Path $directory 'status.txt'),("HABUMI-DATA-UI-V1 {0}`n{1}`nFake result`n`n" -f $request.Nonce,$state),[Text.Encoding]::Unicode)
}
if('__MODE__' -eq 'SucceededError'){exit 7}
exit 0
'@ -replace '__MODE__',$mode
    [IO.File]::WriteAllText((Join-Path $root 'Data-Worker.ps1'),$scriptText)
}
foreach($scenario in @(
    @{Mode='SucceededError';Name='fake-succeeded-error';Message='reported success but exited with an error'},
    @{Mode='NoStatus';Name='fake-no-status';Message='stopped without a verified completion result'},
    @{Mode='WorkingZero';Name='fake-working-zero';Message='stopped without a verified completion result'}
)){
    Set-FakeWorker $scenario.Mode
    $target=Join-Path $base $scenario.Name
    $scenarioOutput=@(& $exe $root Backup $target 'data-gapps.img' 16)
    Check ($LASTEXITCODE -eq 1) "$($scenario.Mode) child is rejected"
    Check (([string]$scenarioOutput[1]).Contains($scenario.Message)) "$($scenario.Mode) reports an appropriate failure"
    Check (Test-Path -LiteralPath (Join-Path ([string]$scenarioOutput[0]) 'request.json')) "$($scenario.Mode) writes its native request"
    Check (-not(Test-Path -LiteralPath $target)) "$($scenario.Mode) does not publish a VM backup"
}
Write-Host "Worker checks: $script:checks passed. Fixture: $base"
