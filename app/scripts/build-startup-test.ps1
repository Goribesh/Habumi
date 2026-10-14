# SPDX-License-Identifier: GPL-2.0-only
# Isolated startup candidate and fake-client/state checks; no installed files replaced.
param([string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source=Join-Path $repoRoot 'app/guscio'
$output=Join-Path $repoRoot 'research/results/ui-startup-20261013'
New-Item -ItemType Directory -Path $output -Force|Out-Null
$compiler=Join-Path $MsysBin 'clang.exe'
$modules=@('main','finestra','vm','comandi','adb','config','registro','tubo','dpi','apk','file','rilascio','archivio','appunti','appunti-trama','varianti','dati','scarica','hyperv')
$files=@($modules|ForEach-Object {Join-Path $source "$_.c"})
& $compiler -std=c11 -Wall -Werror -O2 -mwindows -o (Join-Path $output 'Habumi-async-startup.exe') @files -luser32 -lgdi32 -lcomctl32 -lshell32 -lws2_32 -lwinhttp -lbcrypt
if($LASTEXITCODE){throw 'Isolated startup application build failed.'}
$dependencies=@('registro','archivio','config','varianti','comandi','dpi','tubo')|ForEach-Object {Join-Path $source "$_.c"}
foreach($test in @('adb-ready','vm-ready')){
    # Both tests include adb.c; VM-state test also includes vm.c.
    $exe=Join-Path $output "test-$test.exe"
    & $compiler -std=c11 -O2 -Wall -Werror -o $exe (Join-Path $source "test-$test.c") @dependencies
    if($LASTEXITCODE){throw "Startup test build failed: $test"}
    Push-Location $output
    try{
        $lines=@(& $exe 2>&1);$code=$LASTEXITCODE
        $lines|Set-Content -LiteralPath (Join-Path $output "test-$test.log") -Encoding utf8
        $lines|Write-Output
        if($code){throw "Startup test failed: $test"}
    }finally{Pop-Location}
}
Get-FileHash -LiteralPath (Join-Path $output 'Habumi-async-startup.exe')
