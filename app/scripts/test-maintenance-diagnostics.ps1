# SPDX-License-Identifier: GPL-2.0-only
# Synthetic diagnostics contract tests. Never launches Habumi or QEMU.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$module=Join-Path $repo 'app/release/Profile-Data.psm1'
$checksRoot=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Habumi-Workspace/Builds/Checks'
$base=Join-Path $checksRoot ('maintenance-diag-'+[guid]::NewGuid().ToString('N'))
$root=Join-Path $base 'profile'
New-Item -ItemType Directory -Path (Join-Path $base 'bundles') -Force|Out-Null
New-Item -ItemType Directory -Path (Join-Path $root 'runtime/bin') -Force|Out-Null
New-Item -ItemType Directory -Path (Join-Path $root 'guest/images/android') -Force|Out-Null
Import-Module $module -Force
$script:checks=0
function Check([bool]$Value,[string]$Message){if(-not $Value){throw "FAIL: $Message"};$script:checks++;Write-Host "PASS: $Message"}
function MustFail([scriptblock]$Action,[string]$Message){$failed=$false;try{& $Action|Out-Null}catch{$failed=$true};Check $failed $Message}
function WriteJson([string]$Path,$Value){$parent=[IO.Path]::GetDirectoryName($Path);New-Item -ItemType Directory -Path $parent -Force|Out-Null;[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))}
function Export([string]$Name){$dest=Join-Path (Join-Path $base 'bundles') $Name;$null=Export-HabumiDiagnostics -ProfileRoot $root -Destination $dest;return [pscustomobject]@{Path=$dest;Json=(Get-Content (Join-Path $dest 'diagnostics.json') -Raw|ConvertFrom-Json)}}
function NewTransaction([string]$Id){$dir=Join-Path $root ('maintenance/'+$Id);New-Item -ItemType Directory -Path $dir -Force|Out-Null;return $dir}
function GetRecord($Bundle,[string]$Id){@($Bundle.Json.Maintenance.Transactions|Where-Object Transaction -eq $Id|Select-Object -First 1)[0]}
$id='11111111111111111111111111111111'
try{
    $empty=Export 'maintenance-diag-empty'
    Check ($empty.Json.Maintenance.Schema -eq 'habumi.maintenance-diagnostics.v1' -and $empty.Json.Maintenance.MarkerState -eq 'Missing' -and $empty.Json.Maintenance.DirectoryState -eq 'Missing') 'missing marker and maintenance directory reported'

    $dir=NewTransaction $id
    $stage=Join-Path $dir 'new/guest/images/android/data.img';New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($stage)) -Force|Out-Null
    [IO.File]::WriteAllBytes($stage,[byte[]](1,2,3,4,5))
    WriteJson (Join-Path $dir 'journal.json') @{Schema='habumi.restore.v1';Transaction=$id;Phase='Replacing';Secret='journal-secret'}
    $orphan=Export 'maintenance-diag-orphan';$record=GetRecord $orphan $id
    Check ($record.OrphanStage -and -not $record.MatchesMarker -and $record.JournalState -eq 'Recognized' -and $record.Phase -eq 'Replacing') 'staged image without marker is recognized as orphan'
    Check (@($record.Images|Where-Object {$_.Path -eq 'new/guest/images/android/data.img' -and $_.State -eq 'Present' -and $_.Bytes -eq 5}).Count -eq 1) 'image evidence contains state and length only'
    Check (-not (Get-ChildItem -LiteralPath $orphan.Path -Recurse -File|Where-Object Name -match '^data(-gapps)?\.img$')) 'no staged or original image copied to diagnostics'

    WriteJson (Join-Path $root 'storage-maintenance.lock') @{Schema='habumi.maintenance.v1';Transaction=$id;Secret='marker-secret'}
    $matching=Export 'maintenance-diag-matching';$record=GetRecord $matching $id
    Check ($matching.Json.Maintenance.MarkerState -eq 'Valid' -and $record.MatchesMarker -and -not $record.OrphanStage -and $matching.Json.Maintenance.ActiveTransaction -eq $id) 'matching marker is active and staged image is not orphaned'
    WriteJson (Join-Path $dir 'journal.json') @{Schema='habumi.restore.v1';Transaction=$id;Phase='Committed';Secret='journal-secret'}
    $committed=Export 'maintenance-diag-committed';Check ((GetRecord $committed $id).Phase -eq 'Committed') 'recognized committed transaction phase exported'

    $badCases=@(@{Name='null';Text='null';State='Unsupported'},@{Name='bad';Text='{';State='Unreadable'},@{Name='oversized';Text=('x'*65537);State='Oversized'},@{Name='unsupported';Text='{"Schema":"unknown","Transaction":"11111111111111111111111111111111"}';State='Unsupported'})
    foreach($case in $badCases){[IO.File]::WriteAllText((Join-Path $root 'storage-maintenance.lock'),$case.Text,[Text.UTF8Encoding]::new($false));$bundle=Export ('maintenance-diag-'+$case.Name);Check ($bundle.Json.Maintenance.MarkerState -eq $case.State -and $bundle.Json.Maintenance.Transactions.Count -le 16) ("$($case.Name) marker exports bounded status")}

    Remove-Item -LiteralPath (Join-Path $root 'storage-maintenance.lock') -Force
    $log=Join-Path $dir 'serial.log';$bytes=New-Object byte[] (300*1024);for($i=0;$i -lt $bytes.Length;$i++){$bytes[$i]=[byte]($i%251)};[IO.File]::WriteAllBytes($log,$bytes)
    $tail=Export 'maintenance-diag-log-tail';$capture=(GetRecord $tail $id).Logs|Where-Object Name -eq 'serial.log'|Select-Object -ExpandProperty Capture
    $captured=Join-Path $tail.Path ('maintenance/'+$id+'/serial.log');$actual=[IO.File]::ReadAllBytes($captured);$expected=New-Object byte[] (256*1024);[Array]::Copy($bytes,($bytes.Length-$expected.Length),$expected,0,$expected.Length)
    Check ($capture.State -eq 'Copied' -and $capture.SourceBytes -eq $bytes.Length -and $capture.CapturedBytes -eq 256KB -and $capture.Truncated -and [Convert]::ToBase64String($actual) -eq [Convert]::ToBase64String($expected)) 'serial log export is exactly its capped final 256 KiB'
    $jsonText=[IO.File]::ReadAllText((Join-Path $tail.Path 'diagnostics.json'));Check (-not $jsonText.Contains('marker-secret') -and -not $jsonText.Contains('journal-secret')) 'unknown marker and journal values are excluded'

    for($i=0;$i -lt 17;$i++){$tid=$i.ToString('x').PadLeft(32,'0');$null=NewTransaction $tid}
    $active='ffffffffffffffffffffffffffffffff';$activePath=NewTransaction $active;[IO.Directory]::SetLastWriteTimeUtc($activePath,[DateTime]::UtcNow.AddDays(-7));WriteJson (Join-Path $root 'storage-maintenance.lock') @{Schema='habumi.maintenance.v1';Transaction=$active}
    $many=Export 'maintenance-diag-many';Check ($many.Json.Maintenance.Truncated -and $many.Json.Maintenance.Transactions.Count -eq 16 -and @($many.Json.Maintenance.Transactions|Where-Object {$_.Transaction -eq $active -and $_.MatchesMarker}).Count -eq 1) 'transaction cap is 16 and active marker transaction is prioritized'

    $lock=[IO.File]::Open((Join-Path $root '.habumi-profile.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try{MustFail {Export-HabumiDiagnostics -ProfileRoot $root -Destination (Join-Path $checksRoot ('maintenance-diag-busy-'+[guid]::NewGuid().ToString('N')))} 'busy profile guard refuses diagnostics'}finally{$lock.Dispose()}

    $linkId='eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee';$link=Join-Path $root ('maintenance/'+$linkId);$target=Join-Path $root 'blocked-target';$linked=$false
    New-Item -ItemType Directory -Path $target|Out-Null
    [IO.File]::WriteAllText((Join-Path $target 'serial.log'),'must-not-follow-link')
    try{
        New-Item -ItemType Junction -Path $link -Target $target -ErrorAction Stop|Out-Null;$linked=$true
    }catch{Write-Host 'SKIP: junction creation unavailable'}
    if($linked){
        try{
            $reparse=Export 'maintenance-diag-reparse';$record=GetRecord $reparse $linkId
            Check ($record.JournalState -eq 'Unavailable' -and @($record.Logs|Where-Object {$_.Capture.State -ne 'Unavailable'}).Count -eq 0 -and -not(Test-Path -LiteralPath (Join-Path $reparse.Path ('maintenance/'+$linkId)))) 'transaction reparse path is not followed'
        }finally{
            if([IO.Path]::GetFullPath($link) -ne (Join-Path $root ('maintenance/'+$linkId)) -or ((Get-Item -LiteralPath $link -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0){throw 'Unexpected junction cleanup target'}
            [IO.Directory]::Delete($link)
        }
    }
    Write-Host "Maintenance diagnostics checks: $script:checks passed. Fixture: $root"
}finally{Remove-Module Profile-Data -ErrorAction SilentlyContinue}
