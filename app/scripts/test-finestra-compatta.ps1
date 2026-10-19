# SPDX-License-Identifier: GPL-2.0-only
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source=Join-Path $repoRoot 'app/guscio'
$desktop=[Environment]::GetFolderPath('Desktop')
$output=Join-Path $desktop 'Habumi-Compact-20261019/ui-tests'
New-Item -ItemType Directory -Path $output -Force|Out-Null
$compiler='C:/msys64/clangarm64/bin/clang.exe'
$modules=@('vm','comandi','adb','config','registro','tubo','dpi','apk','file',
    'rilascio','archivio','appunti','appunti-trama','varianti','dati','scarica',
    'hyperv','sessione')
$files=@($modules|ForEach-Object {Join-Path $source "$_.c"})
$exe=Join-Path $output 'test-finestra-compatta.exe'
& $compiler -std=c11 -Wall -Werror -O2 "-I$source" -o $exe `
    (Join-Path $source 'test-finestra-compatta.c') @files `
    -luser32 -lgdi32 -lcomctl32 -lshell32 -lws2_32 -lwinhttp -lbcrypt -ladvapi32
if($LASTEXITCODE){throw 'Compact-window regression test build failed.'}
Push-Location $output
try{
    & $exe
    if($LASTEXITCODE){throw 'Compact-window regression test failed.'}
}finally{Pop-Location}
