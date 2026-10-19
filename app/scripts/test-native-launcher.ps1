# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param([Parameter(Mandatory)][string]$LauncherPath,
      [Parameter(Mandatory)][string]$OutputDirectory,
      [ValidateSet('0','1')][string]$ExpectedChildWindow='1')
$ErrorActionPreference='Stop'
$probe=Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ('launch space '+[char]0x00e8)
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
  Console=[HabumiConsoleProbe]::GetConsoleWindow().ToInt64();Single=$env:HABUMI_SINGLE_WINDOW} |
  ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'result.json') -Encoding UTF8
exit 23
'@
[IO.File]::WriteAllText((Join-Path $probe 'Start-Habumi.ps1'),$script,[Text.UTF8Encoding]::new($false))
foreach($validate in @($true,$false)){
    $arguments=if($validate){@('--validate')}else{@()}
    $options=@{FilePath=$exe;WorkingDirectory=$OutputDirectory;Wait=$true;PassThru=$true}
    if($arguments.Count){$options.ArgumentList=$arguments}
    $process=Start-Process @options
    if($process.ExitCode -ne 23){throw "Child exit code was lost: $($process.ExitCode)"}
    $result=Get-Content -LiteralPath (Join-Path $probe 'result.json') -Raw|ConvertFrom-Json
    if($result.Console -ne 0){throw 'PowerShell received a console window.'}
    if($result.Validate -ne $validate){throw 'Validation argument was not forwarded.'}
    if($result.Root -ne $probe -or $result.WorkingDirectory -ne $probe){throw 'Portable working directory or Unicode path was lost.'}
    if($ExpectedChildWindow -eq '1' -and $result.Single -ne '1'){throw 'Single-window selection was lost.'}
    if($ExpectedChildWindow -eq '0' -and $result.Single){throw 'Separate-window selection was lost.'}
}
# A missing script must fail headlessly when explicitly validating.
Move-Item -LiteralPath (Join-Path $probe 'Start-Habumi.ps1') -Destination (Join-Path $probe 'probe-script.ps1')
$process=Start-Process -FilePath $exe -ArgumentList '--validate' -Wait -PassThru
if($process.ExitCode -ne 1){throw 'Missing-script validation did not fail.'}
Write-Output 'PASS: GUI subsystem; no child console; Unicode/space path; working directory; mode; validation; child exit code; missing script.'
