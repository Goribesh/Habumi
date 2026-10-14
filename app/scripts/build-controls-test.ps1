# SPDX-License-Identifier: GPL-2.0-only
param([string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source=Join-Path $repoRoot 'app/guscio'
$output=Join-Path $repoRoot 'research/results/ui-controls-20261013'
New-Item -ItemType Directory -Path $output -Force|Out-Null
$compiler=Join-Path $MsysBin 'clang.exe'
$modules=@('main','finestra','vm','comandi','adb','config','registro','tubo','dpi','apk','file','rilascio','archivio','appunti','appunti-trama','varianti','dati','scarica','hyperv')
$files=@($modules|ForEach-Object {Join-Path $source "$_.c"})
& $compiler -std=c11 -Wall -Werror -O2 -mwindows -o (Join-Path $output 'Habumi-async-controls.exe') @files -luser32 -lgdi32 -lcomctl32 -lshell32 -lws2_32 -lwinhttp -lbcrypt
if($LASTEXITCODE){throw 'Isolated controls application build failed.'}
$dependencies=@('registro','archivio','config','varianti','dpi')|ForEach-Object {Join-Path $source "$_.c"}
& $compiler -std=c11 -O2 -Wall -Werror -o (Join-Path $output 'test-comandi.exe') (Join-Path $source 'test-comandi.c') @dependencies -luser32 -lgdi32
if($LASTEXITCODE){throw 'Controls test build failed.'}
Push-Location $output
try{
    $lines=@(& (Join-Path $output 'test-comandi.exe') 2>&1);$code=$LASTEXITCODE
    $lines|Set-Content -LiteralPath (Join-Path $output 'test-comandi.log') -Encoding utf8
    $lines|Write-Output
    if($code){throw 'Controls fake-client test failed.'}
}finally{Pop-Location}
$live=Join-Path $output 'live/runtime/bin'
New-Item -ItemType Directory -Path $live -Force|Out-Null
$dependencies+=@((Join-Path $source 'adb.c'),(Join-Path $source 'comandi.c'),(Join-Path $source 'tubo.c'))
& $compiler -std=c11 -O2 -Wall -Werror "-I$source" -o (Join-Path $live 'ui-controls-probe.exe') (Join-Path $repoRoot 'app/probe/ui-controls-probe.c') @dependencies -luser32 -lgdi32
if($LASTEXITCODE){throw 'Live probe build failed.'}
Get-FileHash -LiteralPath (Join-Path $output 'Habumi-async-controls.exe')
