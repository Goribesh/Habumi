# SPDX-License-Identifier: GPL-2.0-only
param([string]$Clang = 'C:\msys64\clangarm64\bin\clang.exe')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$checks = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Habumi-Workspace/Builds/Checks/A3-GUID'
$fixture = Join-Path $checks ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null

try {
    $guscio = Join-Path $repo 'app/guscio'
    $editTest = Join-Path $fixture 'test-config-edit.exe'
    & $Clang -std=c11 -Wall -Werror `
        (Join-Path $guscio 'test-config-edit.c') `
        (Join-Path $guscio 'config-edit.c') -o $editTest
    if ($LASTEXITCODE) { throw 'Config-edit test compilation failed.' }
    & $editTest
    if ($LASTEXITCODE) { throw 'Config-edit test failed.' }

    $saveTest = Join-Path $fixture 'test-config-save.exe'
    & $Clang -std=c11 -Wall -Werror `
        (Join-Path $guscio 'test-config-save.c') `
        (Join-Path $guscio 'config-save.c') `
        (Join-Path $guscio 'config-edit.c') -o $saveTest
    if ($LASTEXITCODE) { throw 'Config-save test compilation failed.' }
    Push-Location $fixture
    try {
        & $saveTest $fixture
        if ($LASTEXITCODE) { throw 'Config-save test failed.' }
    } finally {
        Pop-Location
    }

    $fullscreenTest = Join-Path $fixture 'test-fullscreen.exe'
    & $Clang -std=c11 -Wall -Werror `
        (Join-Path $guscio 'test-fullscreen.c') `
        (Join-Path $guscio 'fullscreen.c') -luser32 -o $fullscreenTest
    if ($LASTEXITCODE) { throw 'Fullscreen test compilation failed.' }
    & $fullscreenTest
    if ($LASTEXITCODE) { throw 'Fullscreen test failed.' }

    & (Join-Path $PSScriptRoot 'test-screenshot.ps1') -Clang $Clang
    if ($LASTEXITCODE) { throw 'Screenshot test failed.' }
    Write-Host 'A3 checks passed.'
} finally {
    Write-Host "Fixture retained: $fixture"
}
