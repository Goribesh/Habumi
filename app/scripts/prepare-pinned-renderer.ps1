# SPDX-License-Identifier: GPL-2.0-only
# Reuse an unchanged published binary with its exact corresponding-source offer.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RuntimeArchive,
      [Parameter(Mandatory)][string]$SourceArchive,
      [Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$lock=Get-Content -LiteralPath (Join-Path $repo 'app/release/development-baseline.json') -Raw|ConvertFrom-Json
if((Get-FileHash -LiteralPath $RuntimeArchive).Hash -ne $lock.RuntimeArchive.SHA256){throw 'Runtime archive differs from the pinned release.'}
if((Get-FileHash -LiteralPath $SourceArchive).Hash -ne $lock.QemuSource.AssetSHA256){throw 'Corresponding-source archive differs from the pinned release.'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $OutputRoot){throw 'Select a new renderer input directory.'}
$relative='runtime/bin/libvirglrenderer-1.dll'
$expected=@($lock.PinnedFiles|Where-Object Path -eq $relative)[0].SHA256
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($RuntimeArchive))
try{
    $entry=$zip.GetEntry($relative)
    if(-not $entry -or $entry.Length -le 0 -or $entry.Length -gt 67108864){throw 'Invalid renderer archive member.'}
    $destination=Join-Path $OutputRoot 'build/src/libvirglrenderer-1.dll'
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent)|Out-Null
    $inputStream=$entry.Open()
    try{
        $outputStream=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputStream.CopyTo($outputStream);$outputStream.Flush($true)}finally{$outputStream.Dispose()}
    }finally{$inputStream.Dispose()}
}finally{$zip.Dispose()}
if((Get-FileHash -LiteralPath $destination).Hash -ne $expected){throw 'Extracted renderer hash differs from the pin.'}
$zip=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($SourceArchive))
try{
    $entry=$zip.GetEntry('native-renderer-patched.tar.gz')
    if(-not $entry){throw 'Renderer corresponding source is missing.'}
    $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
    try{$sourceHash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
}finally{$zip.Dispose()}
$record=[ordered]@{
    Schema='habumi.renderer-input.v1';Mode='reused-published-binary';Rebuilt=$false
    Upstream=$lock.ComponentVersions.RendererUpstream;Commit=$lock.ComponentVersions.RendererCommit
    LibrarySHA256=$expected;RuntimeAsset=$lock.RuntimeArchive.Asset;RuntimeArchiveSHA256=$lock.RuntimeArchive.SHA256
    SourceAsset=$lock.QemuSource.Asset;SourceArchiveSHA256=$lock.QemuSource.AssetSHA256
    SourceMember='native-renderer-patched.tar.gz';SourceMemberSHA256=$sourceHash
    Recipe='app/scripts/prepare-pinned-renderer.ps1'
}
[IO.File]::WriteAllText((Join-Path $OutputRoot 'build-inputs.json'),($record|ConvertTo-Json -Depth 5)+"`n",[Text.UTF8Encoding]::new($false))
Write-Output 'PASS: published renderer and corresponding-source archive verified; binary reused without recompilation.'
