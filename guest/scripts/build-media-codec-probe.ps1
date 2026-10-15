# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param([string]$OutputDirectory,[string]$SdkRoot)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if(-not $SdkRoot){$SdkRoot = Join-Path $env:LOCALAPPDATA 'Android\Sdk'}
$sdkRoot = [IO.Path]::GetFullPath($SdkRoot)
$ndkRoot = Join-Path $sdkRoot 'ndk\28.2.13676358'
$toolchain = Join-Path $ndkRoot 'toolchains\llvm\prebuilt\windows-x86_64'
$clang = Join-Path $toolchain 'bin\clang.exe'
$sysroot = Join-Path $toolchain 'sysroot'
$apiLibraries = Join-Path $sysroot 'usr\lib\aarch64-linux-android\33'
$source = Join-Path $repoRoot 'guest\probe\media-codec-probe.c'
if(-not $OutputDirectory){$OutputDirectory = Join-Path $repoRoot 'research\results\media-codec-probe-20261015'}
$outputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outputFile = Join-Path $outputDirectory 'media-codec-probe'

foreach ($requiredPath in @(
    $clang,
    (Join-Path $sysroot 'usr\include\media\NdkMediaCodec.h'),
    (Join-Path $apiLibraries 'libmediandk.so'),
    (Join-Path $apiLibraries 'liblog.so'),
    $source
)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required Android NDK/API 33 input is missing: $requiredPath"
    }
}

if (Test-Path -LiteralPath $outputDirectory) {
    throw "Refusing to reuse or overwrite existing build output: $outputDirectory"
}
New-Item -ItemType Directory -Path $outputDirectory | Out-Null

$clangArguments = @(
    '--target=aarch64-linux-android33'
    "--sysroot=$sysroot"
    '-O2'
    '-Wall'
    '-Wextra'
    '-Werror'
    '-fPIE'
    '-pie'
    '-Wl,--build-id=sha1'
    $source
    '-o'
    $outputFile
    '-lmediandk'
    '-llog'
)

$taskOldTemp=$env:TEMP
$taskOldTmp=$env:TMP
try {
    # Mantiene gli oggetti temporanei nell'uscita appena creata.
    $env:TEMP=$outputDirectory
    $env:TMP=$outputDirectory
    & $clang @clangArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Android API 33 ARM64 compile failed with exit code $LASTEXITCODE; output preserved"
    }
} finally {$env:TEMP=$taskOldTemp;$env:TMP=$taskOldTmp}

$artifact = Get-Item -LiteralPath $outputFile
$sha256 = (Get-FileHash -LiteralPath $outputFile -Algorithm SHA256).Hash
Write-Output "Built Android ARM64/API 33 codec probe: $($artifact.FullName)"
Write-Output "Size: $($artifact.Length) bytes"
Write-Output "SHA-256: $sha256"
Write-Output 'The binary was compiled only; it was not run on a guest or device.'
