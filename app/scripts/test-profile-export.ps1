# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]param([string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$modulePath=Join-Path $repo 'app/release/Profile-Export.psm1'
$checks=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Habumi-Workspace/Builds/Checks'
$base=Join-Path $checks ('A4-export-'+[guid]::NewGuid().ToString('N'))
$profile=Join-Path $base ('Profile '+[string][char]0x00E9)
$bin=Join-Path $profile 'runtime/bin'
$destinationFolder=Join-Path $base ('Downloads '+[string][char]0x00E9)
New-Item -ItemType Directory -Path $bin,$destinationFolder -Force|Out-Null
$fake=Join-Path $bin 'adb.exe'
$compiler=Join-Path $MsysBin 'clang.exe'
& $compiler -std=c11 -Wall -Werror -O2 -municode -o $fake (Join-Path $PSScriptRoot 'fake-export-adb.c')
if($LASTEXITCODE){throw 'Fake ADB test client compile failed.'}
[IO.File]::WriteAllText((Join-Path $bin 'config.txt'),"# fixture`nadb_port=16666`n",[Text.UTF8Encoding]::new($false))
$remoteSource=Join-Path $base 'android source.bin'
$wrongSource=Join-Path $base 'android wrong source.bin'
$content=[byte[]](0,255,17,0,128,99,10,13,65,226,130,172)
$wrong=[byte[]](1,2,3,4,5)
[IO.File]::WriteAllBytes($remoteSource,$content)
[IO.File]::WriteAllBytes($wrongSource,$wrong)
$hash=(Get-FileHash -LiteralPath $remoteSource -Algorithm SHA256).Hash
$bytes=[string](Get-Item -LiteralPath $remoteSource).Length
$script:checks=0
function Check([bool]$ok,[string]$message){if(-not $ok){throw "FAIL: $message"};$script:checks++;Write-Output "PASS: $message"}
function MustFail([scriptblock]$action,[string]$message){$failed=$false;try{& $action|Out-Null}catch{$failed=$true};Check $failed $message}
function Set-Fake([string]$mode,[string]$source=$remoteSource){
    $env:HABUMI_FAKE_EXPORT_MODE=$mode
    $env:HABUMI_FAKE_EXPORT_HASH=$hash
    $env:HABUMI_FAKE_EXPORT_BYTES=$bytes
    $env:HABUMI_FAKE_EXPORT_SOURCE=$source
    $env:HABUMI_FAKE_EXPORT_LOG=Join-Path $base 'adb-calls.txt'
    $env:HABUMI_FAKE_EXPORT_CALLS=Join-Path $base 'shell-calls.txt'
    Remove-Item -LiteralPath $env:HABUMI_FAKE_EXPORT_LOG,$env:HABUMI_FAKE_EXPORT_CALLS -Force -ErrorAction SilentlyContinue
}
function NoPartial([string]$target){
    return @([IO.Directory]::EnumerateFiles([IO.Path]::GetDirectoryName($target),'*.habumi-export-*.partial')).Count -eq 0
}
Import-Module $modulePath -Force
$unicodeName='caf'+[string][char]0x00E9
$guestPath='/sdcard/quotes/'+$unicodeName+'/it''s "two words".bin'
$destination=Join-Path $destinationFolder ('Export '+[string][char]0x00E9+'.bin')
$savedEnvironment=@{}
foreach($name in @('HABUMI_FAKE_EXPORT_MODE','HABUMI_FAKE_EXPORT_HASH','HABUMI_FAKE_EXPORT_BYTES','HABUMI_FAKE_EXPORT_SOURCE','HABUMI_FAKE_EXPORT_LOG','HABUMI_FAKE_EXPORT_CALLS')){$savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
    Set-Fake 'success'
    $result=Export-HabumiFile -ProfileRoot $profile -GuestPath $guestPath -Destination $destination
    Check ($result.Bytes -eq $content.Length -and $result.SHA256 -eq $hash) 'verified export returns the remote size and SHA-256'
    Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes($destination)) -eq [Convert]::ToBase64String($content)) 'binary data including NUL and non-ASCII bytes is copied exactly'
    $calls=[IO.File]::ReadAllText($env:HABUMI_FAKE_EXPORT_LOG)
    Check ($calls.Contains("-s`t127.0.0.1:16666")) 'config adb_port selects the local ADB serial'
    $quotedRemote="'/sdcard/quotes/$unicodeName/it'\''s `"two words`".bin'"
    Check ($calls.Contains("sha256sum $quotedRemote && stat -c '%s' $quotedRemote")) 'remote metadata command safely quotes Unicode, apostrophes, quotes and spaces'
    Check (([IO.File]::ReadAllText($env:HABUMI_FAKE_EXPORT_CALLS).Trim() -eq '2')) 'remote hash and size are checked both before and after transfer'
    $preserved=Join-Path $destinationFolder 'existing.bin'
    $sentinel=[byte[]](9,8,7,0,6)
    [IO.File]::WriteAllBytes($preserved,$sentinel)
    Set-Fake 'success'
    MustFail {Export-HabumiFile $profile $guestPath $preserved} 'existing destination is refused'
    Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes($preserved)) -eq [Convert]::ToBase64String($sentinel)) 'existing destination bytes remain unchanged'

    $mismatch=Join-Path $destinationFolder 'mismatch.bin'
    Set-Fake 'success' $wrongSource
    MustFail {Export-HabumiFile $profile $guestPath $mismatch} 'local SHA-256 mismatch is rejected'
    Check (-not(Test-Path -LiteralPath $mismatch) -and (NoPartial $mismatch)) 'hash failure publishes no destination and removes only its partial'

    $changed=Join-Path $destinationFolder 'changed.bin'
    Set-Fake 'changed'
    MustFail {Export-HabumiFile $profile $guestPath $changed} 'Android source changing during pull is rejected'
    Check (-not(Test-Path -LiteralPath $changed) -and (NoPartial $changed)) 'changed source publishes no destination and removes its partial'

    $failedPull=Join-Path $destinationFolder 'pull-error.bin'
    Set-Fake 'pullerror'
    MustFail {Export-HabumiFile $profile $guestPath $failedPull} 'ADB pull failure is reported'
    Check (-not(Test-Path -LiteralPath $failedPull) -and (NoPartial $failedPull)) 'pull failure publishes no destination and removes its partial'

    foreach($unsafe in @('/sdcard','/sdcard/','/sdcard/../private','/sdcard/a//b','/sdcard/a/./b','/sdcard/a/../b','/sdcard/a\b','/data/local/file','/storage/emulated/0')){
        MustFail {Export-HabumiFile $profile $unsafe (Join-Path $destinationFolder 'invalid.bin')} "unsafe Android path is rejected: $unsafe"
    }
    MustFail {Export-HabumiFile $profile ("/sdcard/bad"+[char]1) (Join-Path $destinationFolder 'control.bin')} 'control characters in Android paths are rejected'
    MustFail {Export-HabumiFile $profile $guestPath (Join-Path $destinationFolder 'reserved-port.bin') -AdbPort 5555} 'reserved ADB discovery port is rejected'
}finally{
    foreach($name in $savedEnvironment.Keys){[Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process')}
}
Write-Output "PASS: $script:checks export checks completed. Fixture retained at $base"
