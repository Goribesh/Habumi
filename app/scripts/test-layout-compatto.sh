#!/bin/bash
set -euo pipefail

radice="$(cd "$(dirname "$0")/../.." && pwd)"
clang_bin="${CLANG:-clang}"
if [ -n "${HABUMI_COMPACT_TEST_OUT:-}" ]; then
    uscita="$HABUMI_COMPACT_TEST_OUT"
    mkdir -p "$uscita"
    pulisci=0
else
    uscita="$(mktemp -d)"
    pulisci=1
fi
if [ "$pulisci" -eq 1 ]; then
    trap 'rm -rf "$uscita"' EXIT
fi

"$clang_bin" -std=c11 -Wall -Werror -I "$radice/app/guscio" \
    -o "$uscita/test-layout-compatto" \
    "$radice/app/guscio/test-layout-compatto.c"
"$uscita/test-layout-compatto"
