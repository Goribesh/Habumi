# SPDX-License-Identifier: GPL-2.0-only
# Native data UI worker. The request is data, never PowerShell source text.
[CmdletBinding()]param([Parameter(Mandatory)][string]$RequestPath)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
# A GUI launched from PowerShell 7 can inherit its incompatible module path.
# This worker is explicitly hosted by inbox Windows PowerShell, in its own process.
$nativeModules=$PSHOME+'\Modules'
$env:PSModulePath=$nativeModules+';'+$env:SystemRoot+'\System32\WindowsPowerShell\v1.0\Modules'
Import-Module ($nativeModules+'\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
Import-Module ($nativeModules+'\Microsoft.PowerShell.Management\Microsoft.PowerShell.Management.psd1') -Force
$module=Import-Module (Join-Path $PSScriptRoot 'Profile-Data.psm1') -Force -PassThru
$requestFile=[IO.Path]::GetFullPath($RequestPath)
$directory=[IO.Path]::GetDirectoryName($requestFile)
$request=& $module {param($p) Read-HabumiJson $p} $requestFile
if($request.Schema -ne 'habumi.data-ui.v1' -or $request.Nonce -notmatch '^[a-f0-9]{32}$' -or
   [IO.Path]::GetFileName($requestFile) -ne 'request.json' -or
   [IO.Path]::GetFileName($directory) -ne ('Habumi-data-'+$request.Nonce) -or
   $request.Operation -notin @('Backup','Restore','Grow','Recover','Diagnostics')){throw 'Invalid data UI request.'}
$statusPath=Join-Path $directory 'status.txt'
function Write-UiStatus([string]$State,[string]$Message,[string]$Folder=''){
    $message=($Message -replace '[\r\n]',' ')
    if($message.Length -gt 2048){$message=$message.Substring(0,2048)}
    $text="HABUMI-DATA-UI-V1 $($request.Nonce)`n$State`n$message`n$Folder`n"
    $bytes=[Text.Encoding]::Unicode.GetPreamble()+[Text.Encoding]::Unicode.GetBytes($text)
    $temporary=$statusPath+'.new'
    $stream=[IO.File]::Open($temporary,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
    if(Test-Path -LiteralPath $statusPath){[IO.File]::Replace($temporary,$statusPath,[NullString]::Value)}else{[IO.File]::Move($temporary,$statusPath)}
}
try{
    Write-UiStatus Working 'Checking the profile and available space...'
    Set-HabumiDataProgress -Callback {param($phase,$message) Write-UiStatus Working ($phase+': '+$message)}
    $root=[string]$request.ProfileRoot
    switch($request.Operation){
        'Backup' {$result=Backup-HabumiProfile $root ([string]$request.Path);$folder=$result.Backup}
        'Restore' {$result=Restore-HabumiProfile $root ([string]$request.Path);$folder=$result.PreviousFiles}
        'Grow' {$result=Expand-HabumiData -ProfileRoot $root -Image ([string]$request.Image) -SizeGiB ([int]$request.SizeGiB);$folder=[IO.Path]::GetDirectoryName($result.PreviousImage)}
        'Recover' {$result=Repair-HabumiProfile $root;$folder=$result.RecoveryDirectory}
        'Diagnostics' {$result=Export-HabumiDiagnostics $root ([string]$request.Path);$folder=$result.Diagnostics}
    }
    & $module {param($p,$v) Write-HabumiJson $p $v} (Join-Path $directory 'result.json') @{Schema='habumi.data-result.v1';Nonce=$request.Nonce;Operation=$request.Operation;State='Succeeded';Result=$result}
    Write-UiStatus Succeeded ($request.Operation+' completed successfully.') $folder
    exit 0
}catch{
    $message=$_.Exception.Message
    try{
        & $module {param($p,$v) Write-HabumiJson $p $v} (Join-Path $directory 'result.json') @{Schema='habumi.data-result.v1';Nonce=$request.Nonce;Operation=$request.Operation;State='Failed';Error=$message}
        Write-UiStatus Failed $message $directory
    }catch{}
    [Console]::Error.WriteLine($message)
    exit 1
}
