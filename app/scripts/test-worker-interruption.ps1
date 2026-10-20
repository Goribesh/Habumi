# SPDX-License-Identifier: GPL-2.0-only
# Abrupt child-process exit on tiny synthetic files. No UI or VM is launched.
[CmdletBinding()]param()
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$modulePath=Join-Path $repo 'app/release/Profile-Data.psm1'
$module=Import-Module $modulePath -Force -PassThru
$base=Join-Path ([Environment]::GetFolderPath('Desktop')) ('Habumi-Workspace/Builds/Checks/worker-interruption-'+[guid]::NewGuid().ToString('N'))
$root=Join-Path $base ('profile space '+[char]0xe8)
$paths=@('runtime/bin/config.txt','guest/images/android/data-gapps.img','runtime/keymaps/test.txt')
foreach($relative in $paths){
    $path=Join-Path $root $relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($path)) -Force|Out-Null
    [IO.File]::WriteAllText($path,('backup-'+$relative))
}
$backup=Join-Path $base 'verified-backup'
$null=Backup-HabumiProfile $root $backup
$original=@{}
foreach($relative in $paths){
    $path=Join-Path $root $relative
    [IO.File]::WriteAllText($path,('live-before-interruption-'+$relative))
    $original[$relative]=(Get-FileHash -LiteralPath $path).Hash
}
$manifestHash=(Get-FileHash -LiteralPath (Join-Path $backup 'backup.json')).Hash
$childPath=Join-Path $base 'child.ps1'
$child=@'
param([string]$ModulePath,[string]$Root,[string]$Backup,[string]$Mode,[string]$ResultPath)
$ErrorActionPreference='Stop'
Import-Module $ModulePath -Force
if($Mode -eq 'CrashBackup'){
    Set-HabumiDataProgress {param($phase,$message) if($phase -eq 'Copying'){[Environment]::Exit(91)}}
    $null=Backup-HabumiProfile $Root $Backup
    exit 92
}
if($Mode -eq 'CrashRestore'){
    Set-HabumiDataProgress {param($phase,$message) if($phase -eq 'Replacing data'){[Environment]::Exit(91)}}
    $null=Restore-HabumiProfile $Root $Backup
    exit 92
}
if($Mode -eq 'Recover'){
    $result=Repair-HabumiProfile $Root
    [IO.File]::WriteAllText($ResultPath,($result|ConvertTo-Json -Depth 8))
    exit 0
}
exit 93
'@
[IO.File]::WriteAllText($childPath,$child.Replace("`r`n","`n"))
$script:checks=0
function Check([bool]$Value,[string]$Message){if(-not $Value){throw "FAIL: $Message"};$script:checks++;Write-Host "PASS: $Message"}
function RunChild([string]$Mode,[string]$BackupPath,[string]$ResultPath,[int]$Expected){
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $start.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$childPath+'" -ModulePath "'+$modulePath+'" -Root "'+$root+'" -Backup "'+$BackupPath+'" -Mode '+$Mode+' -ResultPath "'+$ResultPath+'"'
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    try{
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit(15000)){
            $process.Kill();$process.WaitForExit()
            throw 'Only the synthetic test child was stopped after its deadline.'
        }
        [IO.File]::WriteAllText((Join-Path $base ($Mode+'.log')),($stdout.Result+"`n"+$stderr.Result))
        Check ($process.ExitCode -eq $Expected) "$Mode exits with the expected status"
    }finally{$process.Dispose()}
}
function CheckLive([string]$Context){
    foreach($relative in $paths){Check ((Get-FileHash -LiteralPath (Join-Path $root $relative)).Hash -eq $original[$relative]) "$Context preserves $relative"}
}
$partial=Join-Path $base 'interrupted-backup'
RunChild CrashBackup $partial (Join-Path $base 'unused.json') 91
Check (-not(Test-Path -LiteralPath $partial)) 'abrupt backup exit never publishes its destination'
$partialDirs=@(Get-ChildItem -LiteralPath $base -Directory -Filter 'interrupted-backup.partial-*')
Check ($partialDirs.Count -eq 1) 'interrupted backup retains its staging directory'
$incomplete=Get-Content -LiteralPath (Join-Path $partialDirs[0].FullName 'backup.json') -Raw|ConvertFrom-Json
Check ($incomplete.State -eq 'Incomplete') 'interrupted backup manifest stays incomplete'
CheckLive 'backup interruption'
Check (-not(Test-Path -LiteralPath (Join-Path $root 'storage-maintenance.lock'))) 'backup interruption requires no live-data recovery'
$guard=& $module {param($r) Open-HabumiDataGuard $r} $root
$guard.Dispose()
Check $true 'process exit releases the profile file guard'

RunChild CrashRestore $backup (Join-Path $base 'unused.json') 91
$markerPath=Join-Path $root 'storage-maintenance.lock'
Check (Test-Path -LiteralPath $markerPath) 'restore crash leaves a durable recovery marker'
$marker=Get-Content -LiteralPath $markerPath -Raw|ConvertFrom-Json
$transaction=Join-Path $root ('maintenance/'+$marker.Transaction)
$journal=Get-Content -LiteralPath (Join-Path $transaction 'journal.json') -Raw|ConvertFrom-Json
Check ($journal.Phase -eq 'Replacing' -and $journal.Files.Count -eq $paths.Count) 'journal identifies the interrupted replacement'
CheckLive 'restore interruption before first move'
foreach($relative in $paths){
    Check ((Get-FileHash -LiteralPath (Join-Path (Join-Path $transaction 'new') $relative)).Hash -eq (Get-FileHash -LiteralPath (Join-Path (Join-Path $backup 'payload') $relative)).Hash) "verified stage retains backup bytes for $relative"
}
$blocked=$false
try{$null=Backup-HabumiProfile $root (Join-Path $base 'blocked-backup')}catch{$blocked=$_.Exception.Message -like '*requires Recover*'}
Check $blocked 'new operations are refused until recovery'
Check (-not(Test-Path -LiteralPath (Join-Path $base 'blocked-backup'))) 'refused operation creates no destination'
$resultPath=Join-Path $base 'fresh-process-recovery.json'
RunChild Recover $backup $resultPath 0
$result=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json
Check ($result.Phase -eq 'Recovered') 'fresh process recovers the interrupted journal'
CheckLive 'fresh-process recovery'
Check (-not(Test-Path -LiteralPath $markerPath)) 'successful recovery clears the startup marker'
Check (Test-Path -LiteralPath (Join-Path $transaction 'new/guest/images/android/data-gapps.img')) 'unused staged disk remains inspectable after recovery'
Check ((Get-FileHash -LiteralPath (Join-Path $backup 'backup.json')).Hash -eq $manifestHash) 'source backup manifest remains unchanged'
$guard=& $module {param($r) Open-HabumiDataGuard $r} $root
$guard.Dispose()
Check $true 'recovered profile permits a new offline operation'
Write-Host "Worker interruption checks: $script:checks passed. Fixture: $base"
