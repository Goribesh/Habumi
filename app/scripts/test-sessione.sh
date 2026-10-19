#!/bin/bash
# Prova il protocollo A1 e il worker senza avviare QEMU.
set -euo pipefail
cd "$(dirname "$0")/../guscio"
out="${HABUMI_A1_TEST_OUT:-${TMPDIR:-/tmp}/habumi-a1-tests}"
mkdir -p "$out"
clang -std=c11 -Wall -Werror -o "$out/test-sessione.exe" \
    test-sessione.c sessione.c registro.c archivio.c -lbcrypt
"$out/test-sessione.exe"
