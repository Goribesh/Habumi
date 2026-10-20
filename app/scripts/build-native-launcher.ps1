# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]param([Parameter(Mandatory)][string]$OutputPath,
    [ValidateSet('0','1')][string]$ChildWindow='1',[string]$MsysBin='C:/msys64/clangarm64/bin')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source=Join-Path $repo 'app/release'
$OutputPath=[IO.Path]::GetFullPath($OutputPath)
$resource=$OutputPath+'.manifest.res';$object=$OutputPath+'.manifest.obj'
& (Join-Path $MsysBin 'llvm-rc.exe') "/I$source" "/FO$resource" (Join-Path $source 'launcher.rc')
if($LASTEXITCODE){throw 'Launcher resource compilation failed.'}
& (Join-Path $MsysBin 'llvm-cvtres.exe') /MACHINE:ARM64 "/OUT:$object" $resource
if($LASTEXITCODE){throw 'Launcher resource conversion failed.'}
& (Join-Path $MsysBin 'clang.exe') -std=c11 -Wall -Werror -O2 -mwindows -municode "-DHABUMI_CHILD_WINDOW=$ChildWindow" -o $OutputPath (Join-Path $source 'launcher.c') (Join-Path $source 'data-ui.c') (Join-Path $source 'data-job.c') $object -luser32 -lgdi32 -lshell32 -lole32 -luuid -lcomctl32 -ladvapi32
if($LASTEXITCODE){throw 'Graphical launcher compilation failed.'}
# Only our two explicitly named temporary compiler outputs are removed.
[IO.File]::Delete($resource);[IO.File]::Delete($object)
