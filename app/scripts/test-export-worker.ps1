# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$worker=Join-Path $repo 'app/release/Export-Worker.ps1'
$base=Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ('export-worker-'+[guid]::NewGuid().ToString('N'))
$profile=Join-Path $base 'profile'
New-Item -ItemType Directory -Path $base,$profile -Force|Out-Null
$powershell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
if(-not(Test-Path -LiteralPath $worker -PathType Leaf) -or
   -not(Test-Path -LiteralPath $powershell -PathType Leaf)){
    throw 'The export worker or inbox Windows PowerShell is missing.'
}
$script:checks=0
function Check([bool]$condition,[string]$message){
    if(-not $condition){throw "FAIL: $message"}
    $script:checks++
}
function New-Request([string]$name,[string]$schema='habumi.export-ui.v1',
                     [string]$guestPath='/sdcard/Download/file.bin',
                     [int]$port=15555,[string]$requestNonce=''){
    $nonce=[guid]::NewGuid().ToString('N')
    $caseFolder=Join-Path $base $name
    New-Item -ItemType Directory -Path $caseFolder|Out-Null
    $directory=Join-Path $caseFolder ('Habumi-export-'+$nonce)
    New-Item -ItemType Directory -Path $directory|Out-Null
    if(-not $requestNonce){$requestNonce=$nonce}
    $request=[ordered]@{
        Schema=$schema
        Nonce=$requestNonce
        ProfileRoot=$profile
        GuestPath=$guestPath
        Destination=(Join-Path $base 'not-created.bin')
        AdbPort=$port
    }
    $requestPath=Join-Path $directory 'request.json'
    $json=$request|ConvertTo-Json -Compress
    [IO.File]::WriteAllText($requestPath,$json,[Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{Nonce=$nonce;Directory=$directory;Path=$requestPath}
}
function Run-Worker($request){
    $output=@(& $powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File $worker -RequestPath $request.Path 2>&1)
    return [pscustomobject]@{ExitCode=$LASTEXITCODE;Output=($output -join "`n")}
}
function Read-Status([string]$path){
    $bytes=[IO.File]::ReadAllBytes($path)
    if($bytes.Length -lt 2 -or ($bytes.Length % 2)){throw 'Status is not UTF-16.'}
    $text=[Text.Encoding]::Unicode.GetString($bytes).TrimStart([char]0xfeff)
    return @($text -split "`n"|ForEach-Object {$_.TrimEnd("`r")})
}

$badSchema=New-Request 'schema' 'habumi.other.v1'
$run=Run-Worker $badSchema
Check ($run.ExitCode -ne 0) 'unknown request schema is rejected'
Check (-not(Test-Path -LiteralPath (Join-Path $badSchema.Directory 'status.txt'))) 'unknown schema cannot publish status'

$badNonce=New-Request 'nonce' 'habumi.export-ui.v1' '/sdcard/Download/file.bin' 15555 ([guid]::NewGuid().ToString('N'))
$run=Run-Worker $badNonce
Check ($run.ExitCode -ne 0) 'nonce that does not match the request directory is rejected'
Check (-not(Test-Path -LiteralPath (Join-Path $badNonce.Directory 'status.txt'))) 'mismatched nonce cannot publish status'

$badPath=New-Request 'path' 'habumi.export-ui.v1' '/sdcard/../private'
$run=Run-Worker $badPath
Check ($run.ExitCode -ne 0) 'path traversal is rejected by the export module'
$status=Read-Status (Join-Path $badPath.Directory 'status.txt')
Check ($status[0] -ceq ('HABUMI-DATA-UI-V1 '+$badPath.Nonce)) 'failure status is bound to the request nonce'
Check ($status[1] -ceq 'Failed' -and $status[2].Contains('unsafe segment')) 'unsafe path reports an atomic failure status'

$controlPath='/sdcard/bad'+[char]1
$badControl=New-Request 'control' 'habumi.export-ui.v1' $controlPath
$run=Run-Worker $badControl
Check ($run.ExitCode -ne 0) 'control characters are rejected by the export module'
$status=Read-Status (Join-Path $badControl.Directory 'status.txt')
Check ($status[0] -ceq ('HABUMI-DATA-UI-V1 '+$badControl.Nonce)) 'control-path failure remains nonce-bound'
Check ($status[2] -notmatch '[\x00-\x1f\x7f-\x9f]') 'status message removes control characters'

$badPort=New-Request 'port' 'habumi.export-ui.v1' '/sdcard/Download/file.bin' 5555
$run=Run-Worker $badPort
Check ($run.ExitCode -ne 0) 'reserved ADB port is rejected before launching ADB'
Check (-not(Test-Path -LiteralPath (Join-Path $badPort.Directory 'status.txt'))) 'invalid port cannot publish a completion status'

Write-Output "PASS: $script:checks export worker request, path, control, port, and nonce checks. Fixture retained at $base"
