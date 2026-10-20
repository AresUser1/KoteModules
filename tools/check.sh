#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
core="${KOTO_CORE_DIR:-$root/../kotogram/kotogram-lib}"
export KOTO_SDK_INCLUDE="${KOTO_SDK_INCLUDE:-$root/sdk/include}"
shopt -s nullglob
for src in "$root"/src/*.c "$root"/src/*.cpp; do
    if [[ "$src" == *.cpp ]]; then compiler="${CXX:-g++}"; standard=c++20
    else compiler="${CC:-cc}"; standard=c11; fi
    "$compiler" "-std=$standard" -fsyntax-only -pthread -I"$KOTO_SDK_INCLUDE" "$src"
done
bash "$root/tests/run_chess.sh"
bash "$root/tests/run_banall.sh"
input_test="$(mktemp)"
trap 'rm -f -- "$input_test"' EXIT
"${CXX:-g++}" -std=c++20 -O1 -I"$KOTO_SDK_INCLUDE" "$root/tests/test_spamban_inputs.cpp" -o "$input_test"
"$input_test"
if [[ -f "$core/tests/test_administration.cpp" ]]; then
    export KOTO_CORE_DIR="$core"
    cmp "$core/host/module_abi.h" "$KOTO_SDK_INCLUDE/module_abi.h"
    cmp "$core/host/module_abi.h" "$root/sdk/include/module_abi.h"
    make -C "$core" -j"${KOTO_JOBS:-2}" CXXFLAGS='-std=c++20 -O1 -g0 -pthread -fcoroutines' lib
    bash "$root/tests/run_administration.sh"
    bash "$root/tests/run_chat_types.sh"
else
    echo 'Core integration tests run in the private Kotogram CI; public CI uses the exported SDK.'
fi
