# SPDX-License-Identifier: GPL-2.0-only
param([string]$Clang = 'C:\msys64\clangarm64\bin\clang.exe')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$root = Join-Path ([Environment]::GetFolderPath('Desktop')) ('Habumi-Workspace/Builds/Checks/screenshot-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    $probe = Join-Path $root 'screenshot probe.exe'
    $adb = Join-Path $root ('fake adb Unicode-' + [char]0xE9 + '.exe')
    & $Clang -std=c11 -Wall -Werror -municode -D_WIN32_WINNT=0x0601 `
        (Join-Path $PSScriptRoot 'test-screenshot-probe.c') `
        (Join-Path $repo 'app/guscio/screenshot.c') `
        -lole32 -luuid -lwindowscodecs -o $probe
    if ($LASTEXITCODE) { throw 'Screenshot probe compilation failed.' }
    & $Clang -std=c11 -Wall -Werror -municode `
        (Join-Path $PSScriptRoot 'fake-adb.c') -o $adb
    if ($LASTEXITCODE) { throw 'Fake adb compilation failed.' }

    $targetDir = Join-Path $root ('destination with spaces-' + [char]0xE9)
    New-Item -ItemType Directory -Path $targetDir | Out-Null
    $target = Join-Path $targetDir 'capture image.png'
    $env:HABUMI_SCREENSHOT_FAKE_MODE = 'success'
    & $probe saved $adb $target
    if ($LASTEXITCODE -ne 0) { throw "Valid PNG capture failed ($LASTEXITCODE)." }
    if (-not (Test-Path -LiteralPath $target)) { throw 'PNG was not published.' }
    $actual = [IO.File]::ReadAllBytes($target)
    if ($actual.Length -ne 68 -or $actual[0] -ne 0x89 -or $actual[1] -ne 0x50) {
        throw 'Published PNG bytes differ from the fake adb payload.'
    }

    $existing = Join-Path $targetDir 'existing.png'
    [IO.File]::WriteAllText($existing, 'keep-existing')
    & $probe existing $adb $existing
    if ($LASTEXITCODE -ne 0 -or [IO.File]::ReadAllText($existing) -ne 'keep-existing') {
        throw 'Existing destination was overwritten or accepted.'
    }

    foreach ($case in @(@('bad', 'invalid.png'), @('nonzero', 'nonzero.png'),
                        @('oversized', 'oversized.png'))) {
        $env:HABUMI_SCREENSHOT_FAKE_MODE = $case[0]
        $path = Join-Path $targetDir $case[1]
        & $probe failed $adb $path
        if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $path)) {
            throw "Failure case '$($case[0])' published output."
        }
    }

    $env:HABUMI_SCREENSHOT_FAKE_MODE = 'timeout'
    & $probe cancel $adb (Join-Path $targetDir 'cancelled.png')
    if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath (Join-Path $targetDir 'cancelled.png'))) {
        throw 'Cancellation did not preserve the destination.'
    }

    $env:HABUMI_SCREENSHOT_FAKE_MODE = 'timeout'
    & $probe failed $adb (Join-Path $targetDir 'timeout.png')
    if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath (Join-Path $targetDir 'timeout.png'))) {
        throw 'Timeout did not preserve the destination.'
    }
    Write-Host 'Screenshot checks passed: valid PNG, Unicode/space paths, no-overwrite, invalid PNG, adb error, cancellation, timeout.'
} finally {
    Write-Host "Fixture retained: $root"
    Remove-Item Env:HABUMI_SCREENSHOT_FAKE_MODE -ErrorAction SilentlyContinue
}
