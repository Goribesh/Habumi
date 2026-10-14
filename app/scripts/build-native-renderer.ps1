# SPDX-License-Identifier: GPL-2.0-only
# Ricostruisce il ponte in una cartella nuova, senza toccare il fork di lavoro.
[CmdletBinding()]
param([string]$UpstreamRepository='', [string]$OutputRoot='',
      [string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(-not $UpstreamRepository){$UpstreamRepository=Join-Path $repoRoot 'research/winq-virgl'}
if(-not $OutputRoot){$OutputRoot=Join-Path $repoRoot 'research/results/release-native-renderer'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $OutputRoot){throw 'Output already exists; choose a new directory.'}
$upstreamCommit='e80354b8f5a0dedc62374b905a181874682659cc'
New-Item -ItemType Directory -Path $OutputRoot|Out-Null
$source=Join-Path $OutputRoot 'source'
New-Item -ItemType Directory -Path $source|Out-Null
& git -C $UpstreamRepository archive --format=tar "--output=$(Join-Path $OutputRoot 'upstream.tar')" $upstreamCommit
if($LASTEXITCODE){throw 'Missing pinned upstream commit; clone cmspam/winq-emu-virglrenderer first.'}
& "$env:SystemRoot/System32/tar.exe" -xf (Join-Path $OutputRoot 'upstream.tar') -C $source
if($LASTEXITCODE){throw 'Upstream extraction failed.'}
# Un repository dedicato impedisce a git apply di usare quello del progetto
# padre e ignorare silenziosamente i percorsi della patch fuori dal prefisso.
& git -C $source init --quiet
if($LASTEXITCODE){throw 'Cannot initialize isolated patch workspace.'}
$patches=@(Get-Item (Join-Path $repoRoot 'research/gfxstream/native-renderer-base.patch'))
# La patch WSI completa e' catturata contro questa base precisa. Le vecchie
# patch individuali si sovrappongono e non vanno riapplicate sopra di essa.
$patches+=Get-Item (Join-Path $repoRoot 'research/gfxstream/venus-android-wsi-native-allocator.patch')
$patches+=Get-Item (Join-Path $repoRoot 'research/gfxstream/native-renderer-warning-fixes.patch')
$records=@()
foreach($patch in $patches){
    $patchInput=Join-Path $OutputRoot ($patch.Name+'.lf')
    [IO.File]::WriteAllText($patchInput,[IO.File]::ReadAllText($patch.FullName).Replace("`r`n","`n"),[Text.UTF8Encoding]::new($false))
    # Il fork contiene blob CRLF; le patch distribuite usano contesto LF.
    foreach($line in [IO.File]::ReadAllLines($patchInput)){
        if($line -match '^--- a/(.+)$'){
            $target=Join-Path $source $Matches[1]
            if(Test-Path -LiteralPath $target){
                [IO.File]::WriteAllText($target,[IO.File]::ReadAllText($target).Replace("`r`n","`n"),[Text.UTF8Encoding]::new($false))
            }
        }
    }
    $applyArgs=@()
    & git -c core.autocrlf=false -C $source apply --no-index @applyArgs --check $patchInput
    if($LASTEXITCODE){throw "Patch check failed: $($patch.Name)"}
    & git -c core.autocrlf=false -C $source apply --no-index @applyArgs $patchInput
    if($LASTEXITCODE){throw "Patch application failed: $($patch.Name)"}
    $records+=@{Name=$patch.Name;SHA256=(Get-FileHash $patch.FullName).Hash;ApplyOptions=$applyArgs}
}
$savedPath=$env:PATH
try{
    $env:PATH="$MsysBin;C:/msys64/usr/bin;$savedPath"
    & (Join-Path $MsysBin 'meson.exe') setup (Join-Path $OutputRoot 'build') $source -Dvenus=true -Dvideo=true -Dtests=false -Dbuildtype=release -Dwerror=true
    if($LASTEXITCODE){throw 'Renderer configuration failed.'}
    & (Join-Path $MsysBin 'ninja.exe') -C (Join-Path $OutputRoot 'build') -j 4
    if($LASTEXITCODE){throw 'Renderer compilation failed.'}
}finally{$env:PATH=$savedPath}
$library=Join-Path $OutputRoot 'build/src/libvirglrenderer-1.dll'
$record=@{Upstream='https://github.com/cmspam/winq-emu-virglrenderer.git';Commit=$upstreamCommit;
    Patches=$records;LibrarySHA256=(Get-FileHash $library).Hash;
    DefaultRuntime='Native WSI allocator on; queue-sync, direct transport and trace off';
    SourceLineEndings='Patched text files normalized to LF before application';
    SourceDirectory='source';BuildDirectory='build'}
[IO.File]::WriteAllText((Join-Path $OutputRoot 'build-inputs.json'),($record|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
Get-FileHash $library
