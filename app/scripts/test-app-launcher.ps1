# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param([Parameter(Mandatory)][string]$LauncherPath,
      [Parameter(Mandatory)][string]$OutputDirectory,
      [ValidateSet('0','1')][string]$ExpectedChildWindow='1')
$ErrorActionPreference='Stop'
$probe=Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ('app-launch space '+[char]0x00e8)
if(Test-Path -LiteralPath $probe){throw 'Select a new test output directory.'}
New-Item -ItemType Directory -Path $probe -Force|Out-Null
$exe=Join-Path $probe 'Habumi.exe'
Copy-Item -LiteralPath $LauncherPath -Destination $exe
$bytes=[IO.File]::ReadAllBytes($exe)
$pe=[BitConverter]::ToInt32($bytes,0x3c)
if([BitConverter]::ToUInt16($bytes,$pe+24+68) -ne 2){throw 'Launcher is not a Windows GUI executable.'}
$script=@'
param([switch]$Validate)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class HabumiConsoleProbe { [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow(); }'
@{Validate=[bool]$Validate;Root=$PSScriptRoot;WorkingDirectory=[Environment]::CurrentDirectory;
  Console=[HabumiConsoleProbe]::GetConsoleWindow().ToInt64();Single=$env:HABUMI_SINGLE_WINDOW;
  LaunchComponent=$env:HABUMI_LAUNCH_COMPONENT} |
  ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'result.json') -Encoding UTF8
exit 23
'@
[IO.File]::WriteAllText((Join-Path $probe 'Start-Habumi.ps1'),$script,[Text.UTF8Encoding]::new($false))

function Invoke-Launcher([string[]]$Arguments,[string]$ExpectedComponent,
                         [bool]$ExpectedValidate,[int]$ExpectedExitCode){
    $options=@{FilePath=$exe;WorkingDirectory=$OutputDirectory;Wait=$true;PassThru=$true}
    if($Arguments.Count){$options.ArgumentList=$Arguments}
    $process=Start-Process @options
    if($process.ExitCode -ne $ExpectedExitCode){throw "Unexpected launcher exit code for $($Arguments -join ' '): got $($process.ExitCode), expected $ExpectedExitCode"}
    if($ExpectedExitCode -eq 1){return}
    $result=Get-Content -LiteralPath (Join-Path $probe 'result.json') -Raw|ConvertFrom-Json
    if($result.Console -ne 0){throw 'PowerShell received a console window.'}
    if($result.Validate -ne $ExpectedValidate){throw 'Validation argument was not forwarded.'}
    if($result.Root -ne $probe -or $result.WorkingDirectory -ne $probe){throw 'Portable working directory or Unicode path was lost.'}
    if($ExpectedChildWindow -eq '1' -and $result.Single -ne '1'){throw 'Single-window selection was lost.'}
    if($ExpectedChildWindow -eq '0' -and $result.Single){throw 'Separate-window selection was lost.'}
    if([string]$result.LaunchComponent -cne $ExpectedComponent){throw "Launch component was not cleared or preserved: '$($result.LaunchComponent)'"}
}

$env:HABUMI_LAUNCH_COMPONENT='stale/.Previous'
$nonAsciiComponent='com.example/.M'+[char]0x00f6+'bius'
Invoke-Launcher @() '' $false 23
Invoke-Launcher @('--app','com.android.settings/.Settings') 'com.android.settings/.Settings' $false 23
Invoke-Launcher @('--app','org.example/.Nested$Activity') 'org.example/.Nested$Activity' $false 23
foreach($arguments in @(
    ,@('--validate','--app','invalid'),
    ,@('--validate','--app','com.example/.Main','--app','com.example/.Other'),
    ,@('--validate','--app','com.example/.Main','--data'),
    ,@('--validate','--data','--app','com.example/.Main'),
    ,@('--validate','--app'),
    ,@('--validate','--app','com.example/.Main;echo'),
    ,@('--validate','--app','com.example/.Has Space'),
    ,@('--validate','--app',$nonAsciiComponent)
)){
    Invoke-Launcher $arguments '' $true 1
}
Move-Item -LiteralPath (Join-Path $probe 'Start-Habumi.ps1') -Destination (Join-Path $probe 'probe-script.ps1')
$process=Start-Process -FilePath $exe -ArgumentList '--validate' -Wait -PassThru
if($process.ExitCode -ne 1){throw 'Missing-script validation did not fail.'}
Write-Output 'PASS: GUI child environment, strict app argument validation, incompatible modes, headless errors, and missing script.'
