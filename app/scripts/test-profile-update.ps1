# SPDX-License-Identifier: GPL-2.0-only
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'
if($PSVersionTable.PSEdition -eq 'Desktop'){$env:PSModulePath=$PSHOME+'\Modules;'+$env:SystemRoot+'\System32\WindowsPowerShell\v1.0\Modules'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Import-Module (Join-Path $repo 'release/Profile-Update.psm1') -Force
$fixture=Join-Path $env:USERPROFILE ('Desktop/Habumi-Workspace/Builds/Checks/A4-profile-update-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture|Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$script:checks=0
function Assert-Update([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message};$script:checks++}
function Write-Bytes([string]$Path,[byte[]]$Bytes){New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Path)) -Force|Out-Null;[IO.File]::WriteAllBytes($Path,$Bytes)}
function Build-Package([string]$Name,[string]$Version,[string]$ExeText,[switch]$Embedded){
    $source=Join-Path $fixture ($Name+'-source');$zip=Join-Path $fixture ($Name+'.zip');$manifest=Join-Path $fixture ($Name+'-files.json')
    New-Item -ItemType Directory -Path $source|Out-Null
    Write-Bytes (Join-Path $source 'Habumi.exe') ([Text.Encoding]::UTF8.GetBytes($ExeText))
    Write-Bytes (Join-Path $source 'runtime/bin/Habumi.exe') ([Text.Encoding]::UTF8.GetBytes($ExeText+'-runtime'))
    Write-Bytes (Join-Path $source 'runtime/bin/qemu-nostro.exe') ([byte[]](1,2,3,4))
    Write-Bytes (Join-Path $source 'runtime/bin/config.txt') ([Text.Encoding]::UTF8.GetBytes('package-default-must-not-install'))
    Write-Bytes (Join-Path $source 'runtime/keymaps/generico.txt') ([Text.Encoding]::UTF8.GetBytes('package-map-must-not-install'))
    Write-Bytes (Join-Path $source 'runtime/empty-data.zip') ([byte[]](9,8,7))
    Write-Bytes (Join-Path $source 'runtime/share/qemu/bios.bin') ([byte[]](5,6,7))
    Write-Bytes (Join-Path $source 'guest/images/kernel-guest-arm64') ([byte[]](11,12))
    $critical=@(foreach($path in @('Habumi.exe','runtime/bin/Habumi.exe')){@{Path=$path;SHA256=(Get-FileHash -LiteralPath (Join-Path $source $path) -Algorithm SHA256).Hash}})
    [IO.File]::WriteAllText((Join-Path $source 'release-profile.json'),(@{Version=$Version;UserDataIncluded=$false;CriticalFiles=$critical}|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    $records=@(Get-ChildItem -LiteralPath $source -File -Recurse|ForEach-Object{$relative=$_.FullName.Substring($source.Length+1).Replace('\','/');@{Path=$relative;Bytes=$_.Length;SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
    $manifestJson=ConvertTo-Json -InputObject $records -Depth 8
    [IO.File]::WriteAllText($manifest,$manifestJson,[Text.UTF8Encoding]::new($false))
    if($Embedded){[IO.File]::WriteAllText((Join-Path $source 'package-files.json'),$manifestJson,[Text.UTF8Encoding]::new($false))}
    $archive=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Create)
    try{foreach($file in Get-ChildItem -LiteralPath $source -File -Recurse){$relative=$file.FullName.Substring($source.Length+1).Replace('\','/');$entry=$archive.CreateEntry($relative);$input=[IO.File]::OpenRead($file.FullName);$output=$entry.Open();try{$input.CopyTo($output)}finally{$output.Dispose();$input.Dispose()}}}finally{$archive.Dispose()}
    return @{Zip=$zip;Manifest=$manifest;SHA=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash}
}
function New-Profile([string]$Path){
    New-Item -ItemType Directory -Path (Join-Path $Path 'runtime/bin') -Force|Out-Null
    Write-Bytes (Join-Path $Path 'runtime/bin/Habumi.exe') ([Text.Encoding]::UTF8.GetBytes('old-runtime'))
    Write-Bytes (Join-Path $Path 'runtime/bin/qemu-nostro.exe') ([byte[]](0,0))
    Write-Bytes (Join-Path $Path 'runtime/bin/config.txt') ([Text.Encoding]::UTF8.GetBytes('user-config'))
    Write-Bytes (Join-Path $Path 'runtime/keymaps/generico.txt') ([Text.Encoding]::UTF8.GetBytes('user-keymap'))
    Write-Bytes (Join-Path $Path 'guest/images/android/data.img') ([Text.Encoding]::UTF8.GetBytes('user-data'))
    Write-Bytes (Join-Path $Path 'guest/logs/session.log') ([Text.Encoding]::UTF8.GetBytes('user-log'))
    [IO.File]::WriteAllText((Join-Path $Path 'release-profile.json'),(@{Version='0.3.0-rc.1'}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
}
function Expect-Failure([scriptblock]$Action,[string]$Message){$failed=$false;try{& $Action}catch{$failed=$true};Assert-Update $failed $Message}

$package=Build-Package 'good' '0.3.0' 'release-utf8-ell'
$profile=Join-Path $fixture 'profile'
New-Profile $profile
$result=Install-HabumiUpdate -ProfileRoot $profile -ArchivePath $package.Zip -ExpectedSHA256 $package.SHA -PackageFilesPath $package.Manifest
Assert-Update ($result.Version -eq '0.3.0') 'Install did not report new version.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $profile 'runtime/bin/Habumi.exe')) -eq 'release-utf8-ell-runtime') 'Software file was not installed.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $profile 'runtime/bin/config.txt')) -eq 'user-config') 'User config changed.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $profile 'runtime/keymaps/generico.txt')) -eq 'user-keymap') 'User keymap changed.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $profile 'guest/images/android/data.img')) -eq 'user-data') 'Guest data changed.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $profile 'guest/logs/session.log')) -eq 'user-log') 'Guest logs changed.'
$saved=Join-Path $result.Previous 'previous/runtime/bin/Habumi.exe'
Assert-Update (Test-Path -LiteralPath $saved) 'Previous software was not retained.'
Rollback-HabumiUpdate -ProfileRoot $profile|Out-Null
Assert-Update ([IO.File]::ReadAllText((Join-Path $profile 'runtime/bin/Habumi.exe')) -eq 'old-runtime') 'Rollback did not restore old software.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $profile 'runtime/bin/config.txt')) -eq 'user-config') 'Rollback changed user config.'

$embeddedPackage=Build-Package 'embedded' '0.3.1' 'embedded-release' -Embedded
$embeddedProfile=Join-Path $fixture 'embedded-profile';New-Profile $embeddedProfile
$embeddedResult=Install-HabumiUpdate -ProfileRoot $embeddedProfile -ArchivePath $embeddedPackage.Zip -ExpectedSHA256 $embeddedPackage.SHA
Assert-Update ($embeddedResult.Version -eq '0.3.1') 'Embedded package inventory was not accepted.'
Rollback-HabumiUpdate -ProfileRoot $embeddedProfile -Transaction $embeddedResult.Transaction|Out-Null

$badProfile=Join-Path $fixture 'bad-hash-profile';New-Profile $badProfile
Expect-Failure {Install-HabumiUpdate -ProfileRoot $badProfile -ArchivePath $package.Zip -ExpectedSHA256 ('0'*64) -PackageFilesPath $package.Manifest} 'Hash mismatch was accepted.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $badProfile 'runtime/bin/Habumi.exe')) -eq 'old-runtime') 'Hash mismatch modified profile.'

$traversalProfile=Join-Path $fixture 'traversal-profile';New-Profile $traversalProfile
$unsafeManifest=Join-Path $fixture 'unsafe-files.json'
[IO.File]::WriteAllText($unsafeManifest,'[{"Path":"../escape","Bytes":1,"SHA256":"'+('0'*64)+'"}]',[Text.UTF8Encoding]::new($false))
Expect-Failure {Install-HabumiUpdate -ProfileRoot $traversalProfile -ArchivePath $package.Zip -ExpectedSHA256 $package.SHA -PackageFilesPath $unsafeManifest} 'Traversal inventory was accepted.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $traversalProfile 'runtime/bin/Habumi.exe')) -eq 'old-runtime') 'Traversal inventory changed profile.'

$conflictProfile=Join-Path $fixture 'conflict-profile';New-Profile $conflictProfile
New-Item -ItemType Directory -Path (Join-Path $conflictProfile 'runtime/share/qemu/bios.bin') -Force|Out-Null
Expect-Failure {Install-HabumiUpdate -ProfileRoot $conflictProfile -ArchivePath $package.Zip -ExpectedSHA256 $package.SHA -PackageFilesPath $package.Manifest} 'File/directory conflict was accepted.'
Assert-Update (Test-Path -LiteralPath (Join-Path $conflictProfile 'runtime/share/qemu/bios.bin') -PathType Container) 'Conflict refusal changed the target directory.'

$openProfile=Join-Path $fixture 'open-profile';New-Profile $openProfile
$lock=[IO.File]::Open((Join-Path $openProfile '.habumi-profile.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try{Expect-Failure {Install-HabumiUpdate -ProfileRoot $openProfile -ArchivePath $package.Zip -ExpectedSHA256 $package.SHA -PackageFilesPath $package.Manifest} 'Open profile was updated.'}finally{$lock.Dispose()}
Assert-Update ([IO.File]::ReadAllText((Join-Path $openProfile 'runtime/bin/Habumi.exe')) -eq 'old-runtime') 'Open-profile refusal changed files.'

$corruptProfile=Join-Path $fixture 'corrupt-profile';New-Profile $corruptProfile
$corrupt=Join-Path $fixture 'corrupt.zip';[IO.File]::WriteAllBytes($corrupt,[byte[]](1,2,3,4,5))
$corruptHash=(Get-FileHash -LiteralPath $corrupt -Algorithm SHA256).Hash
Expect-Failure {Install-HabumiUpdate -ProfileRoot $corruptProfile -ArchivePath $corrupt -ExpectedSHA256 $corruptHash -PackageFilesPath $package.Manifest} 'Corrupt ZIP was accepted.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $corruptProfile 'runtime/bin/Habumi.exe')) -eq 'old-runtime') 'Corrupt ZIP changed profile.'

$recoveryProfile=Join-Path $fixture 'recovery-profile';New-Profile $recoveryProfile
$id=[guid]::NewGuid().ToString('N');$txn=Join-Path $recoveryProfile ('.habumi-updates/'+$id);$previous=Join-Path $txn 'previous/runtime/bin/Habumi.exe'
Write-Bytes $previous ([Text.Encoding]::UTF8.GetBytes('old-runtime'))
$oldRecord=@{Bytes=(Get-Item -LiteralPath $previous).Length;SHA256=(Get-FileHash -LiteralPath $previous -Algorithm SHA256).Hash}
$newPath=Join-Path $recoveryProfile 'runtime/bin/Habumi.exe';Write-Bytes $newPath ([Text.Encoding]::UTF8.GetBytes('interrupted-new'))
$newRecord=@{Bytes=(Get-Item -LiteralPath $newPath).Length;SHA256=(Get-FileHash -LiteralPath $newPath -Algorithm SHA256).Hash}
New-Item -ItemType Directory -Path $txn -Force|Out-Null
$journal=@{Schema='habumi.update.v1';Transaction=$id;Phase='Applying';PreviousVersion='0.3.0-rc.1';NewVersion='0.3.0';Files=@(@{Path='runtime/bin/Habumi.exe';HadOriginal=$true;Original=$oldRecord;New=$newRecord})}
[IO.File]::WriteAllText((Join-Path $txn 'journal.json'),($journal|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $recoveryProfile 'software-update.lock'),(@{Schema='habumi.update-lock.v1';Transaction=$id}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
Recover-HabumiUpdate -ProfileRoot $recoveryProfile|Out-Null
Assert-Update ([IO.File]::ReadAllText($newPath) -eq 'old-runtime') 'Interrupted update was not recovered.'

$tamperProfile=Join-Path $fixture 'tamper-profile';New-Profile $tamperProfile
$tampered=Install-HabumiUpdate -ProfileRoot $tamperProfile -ArchivePath $package.Zip -ExpectedSHA256 $package.SHA -PackageFilesPath $package.Manifest
Write-Bytes (Join-Path $tampered.Previous 'previous/runtime/bin/Habumi.exe') ([Text.Encoding]::UTF8.GetBytes('changed-backup'))
Expect-Failure {Rollback-HabumiUpdate -ProfileRoot $tamperProfile -Transaction $tampered.Transaction} 'Changed rollback backup was accepted.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $tamperProfile 'runtime/bin/Habumi.exe')) -eq 'release-utf8-ell-runtime') 'Tampered backup refusal changed installed file.'

$older=Build-Package 'older' '0.2.3' 'older-release'
$versionProfile=Join-Path $fixture 'version-profile';New-Profile $versionProfile
Expect-Failure {Install-HabumiUpdate -ProfileRoot $versionProfile -ArchivePath $older.Zip -ExpectedSHA256 $older.SHA -PackageFilesPath $older.Manifest} 'Installing an older release was accepted.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $versionProfile 'runtime/bin/Habumi.exe')) -eq 'old-runtime') 'Downgrade refusal changed software.'

$lockedProfile=Join-Path $fixture 'locked-replacement';New-Profile $lockedProfile
$held=[IO.File]::Open((Join-Path $lockedProfile 'runtime/bin/qemu-nostro.exe'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try{Expect-Failure {Install-HabumiUpdate -ProfileRoot $lockedProfile -ArchivePath $package.Zip -ExpectedSHA256 $package.SHA -PackageFilesPath $package.Manifest} 'Locked replacement did not fail.'}finally{$held.Dispose()}
Assert-Update ([IO.File]::ReadAllText((Join-Path $lockedProfile 'runtime/bin/Habumi.exe')) -eq 'old-runtime') 'Failed replacement did not roll back previous writes.'
Assert-Update (-not(Test-Path -LiteralPath (Join-Path $lockedProfile 'software-update.lock'))) 'Verified rollback left an active marker.'
Assert-Update ([IO.File]::ReadAllText((Join-Path $lockedProfile 'guest/images/android/data.img')) -eq 'user-data') 'Failed replacement changed guest data.'

$compatibility=Get-Module Profile-Update
Expect-Failure {& $compatibility {Assert-HabumiUpdateCompatibility ([pscustomobject]@{Version='0.3.0';GuestImageReference='old-image'}) ([pscustomobject]@{Version='0.3.1';GuestImageReference='different-image'})}} 'Incompatible Android image migration was accepted.'

Write-Output ("PASS: $script:checks A4 software update checks. Fixtures retained at "+$fixture)
