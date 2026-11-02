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
$fixture=Join-Path $outputBase ('A4-update-check-'+$runId)
New-Item -ItemType Directory -Path $fixture|Out-Null
$powershell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
if(-not(Test-Path -LiteralPath $powershell -PathType Leaf)){throw 'Windows PowerShell 5.1 was not found.'}

function Assert-Check([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Write-CheckText([string]$Path,[string]$Text){
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Path)) -Force|Out-Null
    [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))
}
function Get-CheckSnapshot([string]$Profile){
    $paths=@('runtime/bin/config.txt','runtime/keymaps/generico.txt','guest/images/android/data.img','guest/logs/session.log')
    return [ordered]@{Files=@(foreach($relative in $paths){$path=Join-Path $Profile $relative;@{Path=$relative;Bytes=(Get-Item -LiteralPath $path).Length;SHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}})}
}
function Invoke-CheckCase([string]$Name,[bool]$Prerelease,[string]$Mode,[string]$Profile){
    $nonce=[guid]::NewGuid().ToString('N')
    $workerRoot=Join-Path $fixture ('Habumi-update-'+$nonce)
    New-Item -ItemType Directory -Path $workerRoot|Out-Null
    [IO.File]::Copy((Join-Path $repo 'release/Update-Worker.ps1'),(Join-Path $workerRoot 'Update-Worker.ps1'),$false)
    $request=@{Schema='habumi.update-ui.v1';Nonce=$nonce;ProfileRoot=$Profile;Operation='Check';Prerelease=$Prerelease;ExpectedVersion=''}
    $requestPath=Join-Path $workerRoot 'request.json'
    Write-CheckText $requestPath ($request|ConvertTo-Json -Depth 8)
    $settings=@{Mode=$Mode;RCVersion='0.3.0-rc.2';RCHash='C75DAE2BBE1A1298ABF9950CF44612B8898321F405C6B935265AC00C67F17837';StableVersion='0.2.9';StableHash=('A'*64)}
    Write-CheckText (Join-Path $workerRoot 'mock.json') ($settings|ConvertTo-Json -Depth 4)
    $wrapper=@'
[CmdletBinding()]param([string]$WorkerPath,[string]$RequestPath)
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'
$mock=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'mock.json') -Raw|ConvertFrom-Json
$global:MockReleases=@()
foreach($item in @(@{Version=$mock.RCVersion;Prerelease=$true;Hash=$mock.RCHash},@{Version=$mock.StableVersion;Prerelease=$false;Hash=$mock.StableHash})){
    $version=[string]$item.Version;$name='Habumi-'+$version+'.zip';$prefix='https://github.com/Goribesh/Habumi/releases/download/'+$version+'/'
    $packageUrl=$prefix+$name;$checksumUrl=$prefix+'SHA256SUMS.txt'
    if($mock.Mode -eq 'wrong-url' -and $item.Prerelease){$packageUrl='https://example.invalid/'+$name}
    $digest='sha256:'+([string]$item.Hash).ToLowerInvariant()
    if($mock.Mode -eq 'digest-mismatch' -and $item.Prerelease){$digest='sha256:'+('B'*64).ToLowerInvariant()}
    $assets=@([pscustomobject]@{name=$name;size=1000;browser_download_url=$packageUrl;digest=$digest},[pscustomobject]@{name='SHA256SUMS.txt';size=128;browser_download_url=$checksumUrl;digest=$null})
    $global:MockReleases+=@([pscustomobject]@{tag_name=$version;draft=$false;prerelease=[bool]$item.Prerelease;html_url='https://github.com/Goribesh/Habumi/releases/tag/'+$version;assets=$assets})
}
function Invoke-RestMethod {
    [CmdletBinding()]param([string]$Uri,$Headers,[int]$TimeoutSec)
    [IO.File]::AppendAllText((Join-Path $PSScriptRoot 'mock-calls.txt'),"REST`n",[Text.UTF8Encoding]::new($false))
    Write-Output -NoEnumerate $global:MockReleases
}
function Invoke-WebRequest {
    [CmdletBinding()]param([string]$Uri,[switch]$UseBasicParsing,$Headers,[int]$TimeoutSec)
    [IO.File]::AppendAllText((Join-Path $PSScriptRoot 'mock-calls.txt'),"WEB`n",[Text.UTF8Encoding]::new($false))
    $version=[regex]::Match($Uri,'/download/([^/]+)/SHA256SUMS\.txt$').Groups[1].Value
    $release=@($global:MockReleases|Where-Object {$_.tag_name -ceq $version})|Select-Object -First 1
    $hash=if($release){[string](@($release.assets|Where-Object {$_.name -ceq ('Habumi-'+$version+'.zip')})[0].digest -replace '^sha256:','')}else{''}
    if($mock.Mode -eq 'digest-mismatch' -and $version -eq $mock.RCVersion){$hash=[string]$mock.RCHash.ToLowerInvariant()}
    $line=$hash+' *Habumi-'+$version+'.zip'
    if($mock.Mode -eq 'duplicate-checksum' -and $version -eq $mock.RCVersion){$line+="`n"+$line}
    return [pscustomobject]@{Content=[Text.Encoding]::UTF8.GetBytes($line+"`n")}
}
. $WorkerPath -RequestPath $RequestPath
'@
    Write-CheckText (Join-Path $workerRoot 'mock-worker.ps1') $wrapper
    $stdout=Join-Path $fixture ($Name+'.stdout.log');$stderr=Join-Path $fixture ($Name+'.stderr.log')
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$powershell
    $start.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+(Join-Path $workerRoot 'mock-worker.ps1')+'" -WorkerPath "'+(Join-Path $workerRoot 'Update-Worker.ps1')+'" -RequestPath "'+$requestPath+'"'
    $start.WorkingDirectory=$workerRoot;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    $outTask=$process.StandardOutput.ReadToEndAsync();$errTask=$process.StandardError.ReadToEndAsync()
    if(-not $process.WaitForExit(10000)){throw "Get-Release mock exceeded its 10-second deadline: $Name"}
    $outTask.Wait();$errTask.Wait()
    [IO.File]::WriteAllText($stdout,$outTask.Result,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($stderr,$errTask.Result,[Text.UTF8Encoding]::new($false))
    $resultPath=Join-Path $workerRoot 'result.json'
    if(-not(Test-Path -LiteralPath $resultPath -PathType Leaf)){throw "Check worker omitted result.json ($Name, exit $($process.ExitCode)): $($errTask.Result)"}
    $result=[IO.File]::ReadAllText($resultPath)|ConvertFrom-Json
    $calls=@();$callsPath=Join-Path $workerRoot 'mock-calls.txt';if(Test-Path -LiteralPath $callsPath){$calls=@(Get-Content -LiteralPath $callsPath)}
    return [pscustomobject]@{Name=$Name;Nonce=$nonce;ExitCode=$process.ExitCode;Result=$result;Stdout=$stdout;Stderr=$stderr;Worker=$workerRoot;Calls=$calls}
}
function Assert-CheckResult($Case,[string]$ExpectedVersion){
    Assert-Check ($Case.ExitCode -eq 0) "Check should pass for $($Case.Name): $($Case.Result.Message)"
    Assert-Check ($Case.Result.OK -eq $true) "Check result should be successful for $($Case.Name)."
    Assert-Check ($Case.Result.Nonce -ceq $Case.Nonce) "Check nonce mismatch for $($Case.Name)."
    Assert-Check ($Case.Result.Release.Version -ceq $ExpectedVersion) "$($Case.Name) selected $($Case.Result.Release.Version), expected $ExpectedVersion."
}
function Assert-CheckFailure($Case,[string]$ExpectedMessage){
    Assert-Check ($Case.Result.OK -eq $false) "$($Case.Name) did not return a failure result."
    Assert-Check ($Case.Result.Nonce -ceq $Case.Nonce) "$($Case.Name) failure nonce mismatch."
    Assert-Check ([string]$Case.Result.Message -like ('*'+$ExpectedMessage+'*')) "$($Case.Name) failed for unexpected reason: $($Case.Result.Message)"
}

$profile=Join-Path $fixture 'profile'
Write-CheckText (Join-Path $profile 'runtime/bin/config.txt') 'config-fixture'
Write-CheckText (Join-Path $profile 'runtime/keymaps/generico.txt') 'keymap-fixture'
Write-CheckText (Join-Path $profile 'guest/images/android/data.img') 'data-fixture'
Write-CheckText (Join-Path $profile 'guest/logs/session.log') 'log-fixture'
$before=Get-CheckSnapshot $profile
$preview=Invoke-CheckCase 'preview-selects-rc' $true 'normal' $profile
Assert-CheckResult $preview '0.3.0-rc.2'
$stable=Invoke-CheckCase 'stable-selects-stable' $false 'normal' $profile
Assert-CheckResult $stable '0.2.9'
$duplicate=Invoke-CheckCase 'duplicate-checksum-rejected' $true 'duplicate-checksum' $profile
Assert-CheckFailure $duplicate 'runtime checksum is missing or ambiguous'
$digest=Invoke-CheckCase 'asset-digest-mismatch-rejected' $true 'digest-mismatch' $profile
Assert-CheckFailure $digest 'asset digest differs'
$url=Invoke-CheckCase 'wrong-asset-url-rejected' $true 'wrong-url' $profile
Assert-CheckFailure $url 'Unexpected release download location'
$expectedCalls=@{ 'preview-selects-rc'=@('REST','WEB');'stable-selects-stable'=@('REST','WEB');'duplicate-checksum-rejected'=@('REST','WEB');'asset-digest-mismatch-rejected'=@('REST','WEB');'wrong-asset-url-rejected'=@('REST') }
foreach($case in @($preview,$stable,$duplicate,$digest,$url)){
    Assert-Check ((@($case.Calls)-join ',') -ceq (@($expectedCalls[$case.Name])-join ',')) "Unexpected mocked web call sequence for $($case.Name)."
}
$after=Get-CheckSnapshot $profile
Assert-Check ((ConvertTo-Json -InputObject $before -Depth 6 -Compress) -ceq (ConvertTo-Json -InputObject $after -Depth 6 -Compress)) 'Offline checks changed the profile fixture.'
$cases=@($preview,$stable,$duplicate,$digest,$url)
$reportLines=@('Habumi updater Check protocol regression','Network calls: 0 (all web cmdlets mocked)','Profile files unchanged: true')
foreach($case in $cases){
    $version='';if($case.Result.PSObject.Properties['Release']){$version=[string]$case.Result.Release.Version}
    $reportLines+=($case.Name+'|exit='+$case.ExitCode+'|nonce='+$case.Result.Nonce+'|ok='+$case.Result.OK+'|version='+$version+'|message='+$case.Result.Message+'|mockCalls='+(@($case.Calls)-join ',')+'|worker='+$case.Worker)
}
[IO.File]::WriteAllLines((Join-Path $fixture 'report.txt'),[string[]]$reportLines,[Text.UTF8Encoding]::new($false))
Write-Output ('Get-Release offline checks passed. Evidence: '+$fixture)
