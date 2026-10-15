# SPDX-License-Identifier: GPL-2.0-only
# Compila il downloader reale con WinHTTP simulato; usa solo file temporanei locali.
param([string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output=Join-Path $repoRoot 'research/results/download-tests-20261015'
New-Item -ItemType Directory -Path $output -Force|Out-Null
$compiler=Join-Path $MsysBin 'clang.exe'
$exe=Join-Path $output 'test-scarica.exe'
& $compiler -std=c11 -O2 -Wall -Werror `
    (Join-Path $repoRoot 'app/guscio/test-scarica.c') -lbcrypt -o $exe
if($LASTEXITCODE){throw 'Downloader failure-path test compile failed.'}
Push-Location $output
try{
    $stdout=Join-Path $output 'test-scarica-out.txt'
    $stderr=Join-Path $output 'test-scarica-error.txt'
    $process=Start-Process -FilePath $exe -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try{
        if(-not $process.WaitForExit(20000)){
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            [void]$process.WaitForExit(3000)
            throw 'Downloader failure-path test exceeded 20 seconds; test client stopped.'
        }
        $exitCode=$process.ExitCode
    }finally{
        $process.Refresh()
        if(-not $process.HasExited){Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue}
        $process.Dispose()
    }
    $lines=@(Get-Content -LiteralPath $stdout)+@(Get-Content -LiteralPath $stderr)
    $lines|Set-Content -LiteralPath (Join-Path $output 'test-scarica.log') -Encoding utf8
    $lines|Write-Output
    if($exitCode){throw 'Downloader failure-path test failed.'}
}finally{Pop-Location}
