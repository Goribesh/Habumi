# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RequestPath)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$nativeModules=$PSHOME+'\Modules'
$env:PSModulePath=$nativeModules+';'+$env:SystemRoot+'\System32\WindowsPowerShell\v1.0\Modules'
Import-Module ($nativeModules+'\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
Import-Module ($nativeModules+'\Microsoft.PowerShell.Management\Microsoft.PowerShell.Management.psd1') -Force

$requestFile=[IO.Path]::GetFullPath($RequestPath)
$directory=[IO.Path]::GetDirectoryName($requestFile)
if([IO.Path]::GetFileName($requestFile) -cne 'request.json'){
    throw 'Invalid export request path.'
}
$fileInfo=Get-Item -LiteralPath $requestFile
if(($fileInfo.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
    throw 'Export request cannot be a link.'
}
$requestBytes=[IO.File]::ReadAllBytes($requestFile)
if($requestBytes.Length -lt 2 -or $requestBytes.Length -gt 32768){
    throw 'Export request size is invalid.'
}
$utf8=[Text.UTF8Encoding]::new($false,$true)
$requestText=$utf8.GetString($requestBytes)
$request=ConvertFrom-Json -InputObject $requestText
if($request.Schema -cne 'habumi.export-ui.v1' -or
   $request.Nonce -cnotmatch '^[a-f0-9]{32}$' -or
   [IO.Path]::GetFileName($directory) -cne ('Habumi-export-'+$request.Nonce) -or
   $request.ProfileRoot -isnot [string] -or -not $request.ProfileRoot -or
   $request.GuestPath -isnot [string] -or -not $request.GuestPath -or
   $request.Destination -isnot [string] -or -not $request.Destination -or
   $request.GuestPath.Length -ge 4096 -or $request.Destination.Length -ge 4096 -or
   $request.AdbPort -isnot [int] -or $request.AdbPort -lt 1024 -or
   $request.AdbPort -gt 65535 -or ($request.AdbPort -ge 5554 -and $request.AdbPort -le 5585)){
    throw 'Invalid export request.'
}
$nonce=[string]$request.Nonce
$statusPath=Join-Path $directory 'status.txt'
function Write-ExportStatus([string]$State,[string]$Message,[string]$Destination=''){
    $builder=[Text.StringBuilder]::new()
    foreach($character in $Message.ToCharArray()){
        if([char]::IsControl($character)){[void]$builder.Append(' ')}
        else{[void]$builder.Append($character)}
    }
    $message=$builder.ToString()
    if($message.Length -gt 2048){$message=$message.Substring(0,2048)}
    if($message.Length -gt 0 -and [char]::IsHighSurrogate($message[$message.Length-1])){
        $message=$message.Substring(0,$message.Length-1)
    }
    $folder=$Destination
    if($folder.Length -gt 4095){$folder=$folder.Substring(0,4095)}
    $text="HABUMI-DATA-UI-V1 $nonce`n$State`n$message`n$folder`n"
    $bytes=[Text.Encoding]::Unicode.GetPreamble()+[Text.Encoding]::Unicode.GetBytes($text)
    $temporary=$statusPath+'.new'
    $stream=[IO.File]::Open($temporary,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
    if(Test-Path -LiteralPath $statusPath){[IO.File]::Replace($temporary,$statusPath,[NullString]::Value)}
    else{[IO.File]::Move($temporary,$statusPath)}
}

$module=Import-Module (Join-Path $PSScriptRoot 'Profile-Export.psm1') -Force -PassThru
try{
    Write-ExportStatus 'Working' 'Checking the Android file and destination...'
    $result=Export-HabumiFile -ProfileRoot ([string]$request.ProfileRoot) `
        -GuestPath ([string]$request.GuestPath) `
        -Destination ([string]$request.Destination) -AdbPort ([int]$request.AdbPort)
    Write-ExportStatus 'Succeeded' 'The Android file was exported and verified.' ([string]$result.Destination)
    exit 0
}catch{
    $message=$_.Exception.Message
    try{Write-ExportStatus 'Failed' $message $directory}catch{}
    [Console]::Error.WriteLine($message)
    exit 1
}
