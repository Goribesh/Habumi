# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]param([string]$OutputDirectory)
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$desktop=[Environment]::GetFolderPath('DesktopDirectory')
if(-not $OutputDirectory){$OutputDirectory=Join-Path $desktop 'Habumi-Workspace/Builds/Checks'}
$outputBase=[IO.Path]::GetFullPath($OutputDirectory)
if(-not(Test-Path -LiteralPath $outputBase -PathType Container)){New-Item -ItemType Directory -Path $outputBase -Force|Out-Null}
$runId=[guid]::NewGuid().ToString('N')
$fixture=Join-Path $outputBase ('A4-update-worker-'+$runId)
New-Item -ItemType Directory -Path $fixture|Out-Null
$powershell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
if(-not(Test-Path -LiteralPath $powershell -PathType Leaf)){throw 'Windows PowerShell 5.1 was not found.'}

function Assert-Worker([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Write-WorkerBytes([string]$Path,[string]$Text){
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Path)) -Force|Out-Null
    [IO.File]::WriteAllBytes($Path,[Text.Encoding]::UTF8.GetBytes($Text))
}
function New-WorkerProfile([string]$Name){
    $profile=Join-Path $fixture $Name
    Write-WorkerBytes (Join-Path $profile 'runtime/bin/Habumi.exe') 'unchanged-software-fixture'
    Write-WorkerBytes (Join-Path $profile 'runtime/bin/config.txt') 'user-config-fixture'
    Write-WorkerBytes (Join-Path $profile 'runtime/keymaps/generico.txt') 'user-keymap-fixture'
    Write-WorkerBytes (Join-Path $profile 'guest/images/android/data.img') 'user-data-fixture'
    Write-WorkerBytes (Join-Path $profile 'guest/logs/session.log') 'user-log-fixture'
    [IO.File]::WriteAllText((Join-Path $profile 'release-profile.json'),(@{Version='0.3.0'}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    return $profile
}
function Get-WorkerSnapshot([string]$Profile){
    $paths=@('runtime/bin/Habumi.exe','runtime/bin/config.txt','runtime/keymaps/generico.txt','guest/images/android/data.img','guest/logs/session.log','release-profile.json')
    return [ordered]@{Files=@(foreach($relative in $paths){$path=Join-Path $Profile $relative;@{Path=$relative;Bytes=(Get-Item -LiteralPath $path).Length;SHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}});SoftwareMarker=(Test-Path -LiteralPath (Join-Path $Profile 'software-update.lock'));MaintenanceMarker=(Test-Path -LiteralPath (Join-Path $Profile 'storage-maintenance.lock'))}
}
function New-WorkerCase([string]$Label,[string]$Operation,[string]$Profile,[string]$Schema,[string]$FolderNonce,[string]$RequestNonce){
    # Keep the worker copy on the caller workspace: spawned Windows PowerShell
    # processes receive a private TEMP namespace in the desktop test sandbox.
    $workerRoot=Join-Path $fixture ('Habumi-update-'+$FolderNonce)
    if(Test-Path -LiteralPath $workerRoot){throw "Temporary worker identity already exists: $workerRoot"}
    New-Item -ItemType Directory -Path $workerRoot|Out-Null
    foreach($name in @('Update-Worker.ps1','Profile-Update.psm1','Profile-Data.psm1','Profile-SystemInfo.ps1','Profile-Growth.ps1')){
        [IO.File]::Copy((Join-Path $repo ('release/'+$name)),(Join-Path $workerRoot $name),$false)
    }
    $request=@{Schema=$Schema;Nonce=$RequestNonce;ProfileRoot=$Profile;Operation=$Operation;Prerelease=$false;ExpectedVersion=''}
    $requestPath=Join-Path $workerRoot 'request.json'
    [IO.File]::WriteAllText($requestPath,($request|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $fixture ($Label+'.worker-path.txt')),$workerRoot,[Text.UTF8Encoding]::new($false))
    $stdout=Join-Path $fixture ($Label+'.stdout.log');$stderr=Join-Path $fixture ($Label+'.stderr.log')
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$powershell
    $start.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+(Join-Path $workerRoot 'Update-Worker.ps1')+'" -RequestPath "'+$requestPath+'"'
    $start.WorkingDirectory=$workerRoot;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    $outTask=$process.StandardOutput.ReadToEndAsync();$errTask=$process.StandardError.ReadToEndAsync()
    if(-not $process.WaitForExit(30000)){throw "Offline worker timed out: $Label"}
    $outTask.Wait();$errTask.Wait()
    [IO.File]::WriteAllText($stdout,$outTask.Result,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($stderr,$errTask.Result,[Text.UTF8Encoding]::new($false))
    $resultPath=Join-Path $workerRoot 'result.json'
    if(-not(Test-Path -LiteralPath $resultPath -PathType Leaf)){throw "Worker did not write result.json for $Label (exit $($process.ExitCode), folder $workerRoot). stderr: $($errTask.Result)"}
    $result=[IO.File]::ReadAllText($resultPath)|ConvertFrom-Json
    return [pscustomobject]@{ExitCode=$process.ExitCode;Result=$result;Folder=$workerRoot;Stdout=$stdout;Stderr=$stderr}
}
function Assert-WorkerFailure($Invocation,[string]$Nonce,[string]$MessagePart,[string]$Label){
    Assert-Worker ($Invocation.ExitCode -ne 0) "$Label unexpectedly succeeded."
    Assert-Worker ($Invocation.Result.OK -eq $false) "$Label result did not report failure."
    Assert-Worker ($Invocation.Result.Nonce -ceq $Nonce) "$Label result nonce did not match its request."
    Assert-Worker (-not [string]::IsNullOrWhiteSpace([string]$Invocation.Result.Message)) "$Label result omitted its error message."
    if($MessagePart){Assert-Worker ([string]$Invocation.Result.Message -like ('*'+$MessagePart+'*')) "$Label failed for an unexpected reason: $($Invocation.Result.Message)"}
}

$profile=New-WorkerProfile 'profile'
$before=Get-WorkerSnapshot $profile

$nonce=[guid]::NewGuid().ToString('N')
$recover=New-WorkerCase 'recover-no-marker' 'Recover' $profile 'habumi.update-ui.v1' $nonce $nonce
Assert-WorkerFailure $recover $nonce 'No interrupted software update' 'Recover without marker'

$nonce=[guid]::NewGuid().ToString('N')
$schema=New-WorkerCase 'bad-schema' 'Recover' $profile 'invalid-schema' $nonce $nonce
Assert-WorkerFailure $schema $nonce 'Invalid updater request identity' 'Bad schema'

$folderNonce=[guid]::NewGuid().ToString('N');$requestNonce=[guid]::NewGuid().ToString('N')
$badFolder=New-WorkerCase 'bad-nonce-folder' 'Recover' $profile 'habumi.update-ui.v1' $folderNonce $requestNonce
Assert-WorkerFailure $badFolder $requestNonce 'Invalid updater request identity' 'Bad nonce folder'

$nonce=[guid]::NewGuid().ToString('N')
$unknown=New-WorkerCase 'unknown-operation' 'Destroy' $profile 'habumi.update-ui.v1' $nonce $nonce
Assert-WorkerFailure $unknown $nonce 'Unknown updater operation' 'Unknown operation'

$nonce=[guid]::NewGuid().ToString('N')
$rollback=New-WorkerCase 'rollback-no-retained-version' 'Rollback' $profile 'habumi.update-ui.v1' $nonce $nonce
Assert-WorkerFailure $rollback $nonce 'No retained software version' 'Rollback without retained version'

$after=Get-WorkerSnapshot $profile
Assert-Worker ((ConvertTo-Json -InputObject $before -Depth 8 -Compress) -ceq (ConvertTo-Json -InputObject $after -Depth 8 -Compress)) 'Rejected worker requests changed profile files or update markers.'
$dataSource=[IO.File]::ReadAllText((Join-Path $repo 'release/Profile-Data.psm1'))
$uiSource=[IO.File]::ReadAllText((Join-Path $repo 'release/Update-Habumi.ps1'))
$needsGrowth=$dataSource.Contains('Join-Path $PSScriptRoot ''Profile-Growth.ps1''')
$uiCopiesGrowth=$uiSource.Contains('''Profile-Growth.ps1''')
$integrationGap=if($needsGrowth -and -not $uiCopiesGrowth){'Update-Habumi.ps1 omits Profile-Growth.ps1 although Profile-Data.psm1 dot-sources it.'}else{''}
$report=@{Schema='habumi.update-worker-test.v1';RunId=$runId;Fixture=$fixture;WorkerFolders=@($recover.Folder,$schema.Folder,$badFolder.Folder,$unknown.Folder,$rollback.Folder);Results=@(foreach($case in @($recover,$schema,$badFolder,$unknown,$rollback)){@{ExitCode=$case.ExitCode;OK=$case.Result.OK;Nonce=$case.Result.Nonce;Message=$case.Result.Message;Stdout=$case.Stdout;Stderr=$case.Stderr}});ProfileUnchanged=$true;IntegrationGap=$integrationGap}
[IO.File]::WriteAllText((Join-Path $fixture 'report.json'),($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output ('Update worker protocol checks passed. Evidence: '+$fixture)
if($integrationGap){Write-Output ('Integration issue: '+$integrationGap)}
