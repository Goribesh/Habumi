# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]param()
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$module=Import-Module (Join-Path $repo 'app/release/Profile-Data.psm1') -Force -PassThru
$base=Join-Path ([Environment]::GetFolderPath('Desktop')) ('Habumi-Workspace/Builds/Checks/recovery-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $base -Force|Out-Null
$script:checks=0
function Check([bool]$Value,[string]$Message){if(-not $Value){throw "FAIL: $Message"};$script:checks++;Write-Output "PASS: $Message"}
function MustFail([scriptblock]$Action,[string]$Message){$failed=$false;try{& $Action|Out-Null}catch{$failed=$true};Check $failed $Message}
function Put([string]$Path,[string]$Text){New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Path)) -Force|Out-Null;[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
function New-Interrupted([string]$Name,[string]$Phase='Replacing'){
    $root=Join-Path $base $Name;$id=[guid]::NewGuid().ToString('N');$directory=Join-Path $root ('maintenance/'+$id)
    $paths=@('runtime/bin/config.txt','guest/images/android/data-gapps.img','runtime/keymaps/new.txt')
    $records=@()
    for($i=0;$i -lt $paths.Count;$i++){
        $old=$null
        if($i -lt 2){
            $saved=Join-Path (Join-Path $directory 'original') $paths[$i];Put $saved ('original-'+$i)
            $old=@{Bytes=(Get-Item -LiteralPath $saved).Length;SHA256=(Get-FileHash -LiteralPath $saved).Hash}
        }
        $target=Join-Path $root $paths[$i];Put $target ('replacement-'+$i)
        $new=@{Bytes=(Get-Item -LiteralPath $target).Length;SHA256=(Get-FileHash -LiteralPath $target).Hash}
        $records+=@{Path=$paths[$i];HadOriginal=($i -lt 2);Original=$old;HasNew=$true;New=$new}
    }
    & $module {param($r,$d,$id,$phase,$records)
        Write-HabumiJson (Join-Path $d 'journal.json') @{Schema='habumi.restore.v1';Transaction=$id;Phase=$phase;Files=$records}
        Write-HabumiJson (Join-Path $r 'storage-maintenance.lock') @{Schema='habumi.maintenance.v1';Transaction=$id}
    } $root $directory $id $Phase $records
    return @{Root=$root;Directory=$directory;Paths=$paths}
}
$case=New-Interrupted 'replacing'
MustFail {Backup-HabumiProfile $case.Root (Join-Path $base 'blocked-backup')} 'pending restore blocks other data operations'
$result=Repair-HabumiProfile $case.Root
Check ($result.Phase -eq 'Recovered') 'interrupted replacement rolls back'
for($i=0;$i -lt 2;$i++){Check ([IO.File]::ReadAllText((Join-Path $case.Root $case.Paths[$i])) -eq ('original-'+$i)) 'original contents recovered'}
Check (-not(Test-Path -LiteralPath (Join-Path $case.Root $case.Paths[2]))) 'new-only file removed from active profile'
Check (Test-Path -LiteralPath (Join-Path (Join-Path $case.Directory 'interrupted') $case.Paths[2])) 'interrupted replacement retained for inspection'
Check (-not(Test-Path -LiteralPath (Join-Path $case.Root 'storage-maintenance.lock'))) 'successful recovery clears startup marker'
$case=New-Interrupted 'partial-rollback'
$saved=Join-Path (Join-Path $case.Directory 'original') $case.Paths[0]
$target=Join-Path $case.Root $case.Paths[0]
[IO.File]::Move($target,(Join-Path $case.Directory 'already-rejected.txt'));[IO.File]::Move($saved,$target)
$null=Repair-HabumiProfile $case.Root
Check ([IO.File]::ReadAllText($target) -eq 'original-0') 'recovery tolerates an already restored original'
Check ([IO.File]::ReadAllText((Join-Path $case.Root $case.Paths[1])) -eq 'original-1') 'recovery resumes remaining rollback'
$case=New-Interrupted 'committed' 'Committed'
$null=Repair-HabumiProfile $case.Root
Check ([IO.File]::ReadAllText((Join-Path $case.Root $case.Paths[0])) -eq 'replacement-0') 'committed recovery retains verified replacement'
Check (Test-Path -LiteralPath (Join-Path (Join-Path $case.Directory 'original') $case.Paths[0])) 'committed recovery retains previous original'
Check (-not(Test-Path -LiteralPath (Join-Path $case.Root 'storage-maintenance.lock'))) 'committed recovery finalizes startup marker'
$case=New-Interrupted 'corrupt-original'
Put (Join-Path (Join-Path $case.Directory 'original') $case.Paths[1]) 'corrupt'
MustFail {Repair-HabumiProfile $case.Root} 'corrupt original refuses recovery before any rollback'
Check ([IO.File]::ReadAllText((Join-Path $case.Root $case.Paths[0])) -eq 'replacement-0') 'failed recovery leaves other current files untouched'
Check (Test-Path -LiteralPath (Join-Path $case.Root 'storage-maintenance.lock')) 'failed recovery continues blocking startup'
MustFail {& $module {param($root) Assert-HabumiSpace $root 1TB} $base} 'insufficient-space preflight rejects an impossible allocation'
# A real Windows sharing violation must not publish a partial backup or replace data.
$case=New-Interrupted 'sharing'
$null=Repair-HabumiProfile $case.Root
$held=[IO.File]::Open((Join-Path $case.Root $case.Paths[1]),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try{MustFail {Backup-HabumiProfile $case.Root (Join-Path $base 'sharing-backup')} 'in-use image refuses backup'}finally{$held.Dispose()}
Check (-not(Test-Path -LiteralPath (Join-Path $base 'sharing-backup'))) 'failed backup is not published'
$null=Backup-HabumiProfile $case.Root (Join-Path $base 'sharing-backup')
$held=[IO.File]::Open((Join-Path $case.Root $case.Paths[1]),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try{MustFail {Restore-HabumiProfile $case.Root (Join-Path $base 'sharing-backup')} 'in-use original refuses restore'}finally{$held.Dispose()}
Check ([IO.File]::ReadAllText((Join-Path $case.Root $case.Paths[0])) -eq 'original-0') 'in-use restore leaves earlier files unchanged'
Check (-not(Test-Path -LiteralPath (Join-Path $case.Root 'storage-maintenance.lock'))) 'failed preflight needs no recovery marker'
# Grow uses one data-image entry in the same journal. Simulate interruption
# after moving the original aside, before installing the enlarged staged disk.
$case=New-Interrupted 'growth-before-install'
$journalPath=Join-Path $case.Directory 'journal.json'
$journal=Get-Content -LiteralPath $journalPath -Raw|ConvertFrom-Json
$journal.Files=@($journal.Files|Where-Object Path -eq 'guest/images/android/data-gapps.img')
$journal|Add-Member -NotePropertyName Operation -NotePropertyValue Grow
& $module {param($p,$j) Write-HabumiJson $p $j} $journalPath $journal
$target=Join-Path $case.Root 'guest/images/android/data-gapps.img'
$staged=Join-Path $case.Directory 'enlarged-staged.img'
[IO.File]::Move($target,$staged)
$result=Repair-HabumiProfile $case.Root
Check ($result.Phase -eq 'Recovered') 'interrupted growth before installation recovers'
Check ([IO.File]::ReadAllText($target) -eq 'original-1') 'interrupted growth restores original disk'
Check (Test-Path -LiteralPath $staged) 'unused staged growth remains available for inspection'
Check (-not(Test-Path -LiteralPath (Join-Path $case.Root 'storage-maintenance.lock'))) 'recovered growth releases startup marker'
Write-Output "Recovery checks: $script:checks passed. Fixture: $base"
