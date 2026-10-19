#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Compila soltanto l'albero isolato preparato dalla ricetta PowerShell.
set -euo pipefail
source_dir="$(cygpath -u "$1")"
build_dir="$(cygpath -u "$2")"
jobs="$3"
if [ "$(clang -dumpmachine)" != "aarch64-w64-windows-gnu" ]; then
    echo 'CLANGARM64 is required.' >&2
    exit 1
fi
mkdir "$build_dir"
cd "$build_dir"
"$source_dir/configure" \
    --prefix=/clangarm64 --bindir=bin --with-suffix=qemu \
    --target-list=aarch64-softmmu --enable-virglrenderer --enable-opengl \
    --enable-sdl --enable-whpx --disable-docs --disable-guest-agent
ninja qemu-system-aarch64.exe -j "$jobs"
./qemu-system-aarch64.exe --version
