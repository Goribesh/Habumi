# SPDX-License-Identifier: GPL-2.0-only
# No virtual machine is started. Query results below are injected test doubles.
[CmdletBinding()]param()
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$module=Import-Module (Join-Path $repo 'app/release/Profile-Data.psm1') -Force -PassThru
$root=Join-Path ([Environment]::GetFolderPath('Desktop')) ('Habumi-Workspace/Builds/Checks/maintenance-guard-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $root 'runtime/bin') -Force|Out-Null
New-Item -ItemType Directory -Path (Join-Path $root 'guest/images/android') -Force|Out-Null
$data=Join-Path $root 'guest/images/android/data-gapps.img'
[IO.File]::WriteAllText($data,'guard-test-original')
$hash=(Get-FileHash -LiteralPath $data).Hash
$script:checks=0
function Check([bool]$Value,[string]$Message){if(-not $Value){throw "FAIL: $Message"};$script:checks++;Write-Host "PASS: $Message"}
function MustFailLike([scriptblock]$Action,[string]$Pattern,[string]$Message){
    $caught='';try{& $Action|Out-Null}catch{$caught=$_.Exception.Message}
    Check ($caught -like $Pattern) $Message
}
$originalQuery=& $module {${function:Get-HabumiQemuProcesses}}
# The real, bounded process query must be available before we replace it.
$null=& $module {Get-HabumiQemuProcesses}
Check $true 'real process query completes without starting a VM'
& $module {function script:Get-HabumiQemuProcesses {return @()}}
$first=$null;$second=$null
try{
    $first=& $module {Open-HabumiMaintenanceGuard}
    Check ($null -ne $first) 'idle session acquires maintenance gate'
    MustFailLike {& $module {Open-HabumiMaintenanceGuard}} '*Close all Habumi windows*' 'second maintenance operation is refused'
    # This is the legacy shell handshake: CreateMutex sees an existing object.
    $created=$true
    $second=[Threading.Mutex]::new($false,'AndroidRuntimeGuscio.istanza',[ref]$created)
    Check (-not $created) 'legacy unprefixed shell name observes the same gate'
}finally{if($second){$second.Dispose()};if($first){$first.Dispose()}}
$after=& $module {Open-HabumiMaintenanceGuard};$after.Dispose()
Check $true 'gate is released when its handles close'
& $module {function script:Get-HabumiQemuProcesses {return @([pscustomobject]@{ProcessId=123;Name='qemu-nostro.exe'})}}
MustFailLike {Expand-HabumiData $root data-gapps.img 8} '*leftover processes*' 'orphan QEMU blocks growth before staging'
Check (-not(Test-Path -LiteralPath (Join-Path $root 'maintenance'))) 'blocked growth creates no staged disk'
Check ((Get-FileHash -LiteralPath $data).Hash -eq $hash) 'orphan refusal leaves original bytes intact'
& $module {function script:Get-HabumiQemuProcesses {throw 'Injected process-query failure'}}
MustFailLike {Expand-HabumiData $root data-gapps.img 8} '*Cannot verify that QEMU is stopped*' 'unavailable process query fails closed'
Check (-not(Test-Path -LiteralPath (Join-Path $root 'maintenance'))) 'query failure creates no staged disk'
& $module {function script:Get-HabumiQemuProcesses {return @()}}
$after=& $module {Open-HabumiMaintenanceGuard};$after.Dispose()
Check $true 'failed preflights release their named gate'
# A separate process holds the same object used by a running shell. File
# handshakes make this deterministic; the child exits normally on release.
$childPath=Join-Path $root 'gate-holder.ps1'
$childCode=@'
param([string]$Root)
$created=$false
$gate=[Threading.Mutex]::new($false,'AndroidRuntimeGuscio.istanza',[ref]$created)
try{
    if(-not $created){exit 2}
    [IO.File]::WriteAllText((Join-Path $Root 'ready.txt'),'ready')
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    while(-not(Test-Path -LiteralPath (Join-Path $Root 'release.txt')) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 25}
}finally{$gate.Dispose()}
'@
[IO.File]::WriteAllText($childPath,$childCode)
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$start.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$childPath+'" -Root "'+$root+'"'
$start.UseShellExecute=$false;$start.CreateNoWindow=$true
$child=[Diagnostics.Process]::Start($start)
try{
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    while(-not(Test-Path -LiteralPath (Join-Path $root 'ready.txt')) -and -not $child.HasExited -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 25}
    Check (Test-Path -LiteralPath (Join-Path $root 'ready.txt')) 'separate shell-like process acquires gate'
    MustFailLike {Expand-HabumiData $root data-gapps.img 8} '*Close all Habumi windows*' 'running shell in another process blocks growth'
}finally{
    [IO.File]::WriteAllText((Join-Path $root 'release.txt'),'release')
    $ended=$child.WaitForExit(10000)
    $child.Dispose()
    if(-not $ended){throw 'Test gate holder did not exit normally; inspect the fixture.'}
}
$after=& $module {Open-HabumiMaintenanceGuard};$after.Dispose()
Check $true 'child process exit releases its runtime gate'
& $module {param($Query) Set-Item Function:script:Get-HabumiQemuProcesses $Query} $originalQuery
Write-Host "Maintenance guard checks: $script:checks passed. Fixture: $root"
