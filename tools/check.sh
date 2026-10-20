#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export KOTO_CORE_DIR="${KOTO_CORE_DIR:-$root/../kotogram/kotogram-lib}"
export KOTO_SDK_INCLUDE="${KOTO_SDK_INCLUDE:-$KOTO_CORE_DIR/../kote_module_sdk/include}"
cmp "$KOTO_CORE_DIR/host/module_abi.h" "$KOTO_SDK_INCLUDE/module_abi.h"
make -C "$KOTO_CORE_DIR" -j"${KOTO_JOBS:-2}" \
    CXXFLAGS='-std=c++20 -O1 -g0 -Wall -Wextra -Wno-unused-parameter -fcoroutines -pthread' lib
shopt -s nullglob
for src in "$root"/src/*.c "$root"/src/*.cpp; do
    if [[ "$src" == *.cpp ]]; then compiler="${CXX:-g++}"; standard=c++20
    else compiler="${CC:-cc}"; standard=c11; fi
    "$compiler" "-std=$standard" -fsyntax-only -pthread -I"$KOTO_SDK_INCLUDE" "$src"
done
bash "$root/tests/run_chess.sh"
bash "$root/tests/run_administration.sh"
