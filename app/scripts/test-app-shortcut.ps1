# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]param([string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source=Join-Path $repoRoot 'app/guscio'
$checks=Join-Path $env:USERPROFILE 'Desktop/Habumi-Workspace/Builds/Checks'
$runRoot=Join-Path $checks ('A4-shortcut-'+[string][char]0x00E9+'-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot -Force|Out-Null
$compiler=Join-Path $MsysBin 'clang.exe'
$exe=Join-Path $runRoot 'test-app-shortcut.exe'
& $compiler -std=c11 -Wall -Werror -O2 -municode -o $exe `
    (Join-Path $PSScriptRoot 'test-app-shortcut.c') `
    (Join-Path $source 'app-shortcut.c') `
    (Join-Path $source 'app-target.c') `
    -lole32 -luuid -lshell32
if($LASTEXITCODE){throw 'Application shortcut test compile failed.'}
$lines=@(& $exe $runRoot 2>&1);$code=$LASTEXITCODE
$lines|Write-Output
if($code){throw "Application shortcut test failed; fixture retained at $runRoot"}
Write-Output "Test fixture: $runRoot"
