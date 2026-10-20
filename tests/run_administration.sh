#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
core_dir="${KOTO_CORE_DIR:-$repo_dir/../kotogram/kotogram-lib}"
sdk_include="${KOTO_SDK_INCLUDE:-$repo_dir/../kote_module_sdk/include}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/kotobas-admin-test.XXXXXX")"
trap 'rm -rf -- "$test_dir"' EXIT
mkdir "$test_dir/modules"
if [[ ! -f "$core_dir/libkotogram.a" ]]; then
    echo 'Сначала соберите ядро: make lib в KOTO_CORE_DIR.' >&2; exit 1
fi
flags=(-std=c++20 -O0 -g0 -pthread)
if [[ "${1:-}" == --sanitize ]]; then
    flags+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
elif [[ $# -gt 0 ]]; then
    echo 'Использование: bash tests/run_administration.sh [--sanitize]' >&2; exit 2
fi
"${CXX:-g++}" "${flags[@]}" -shared -fPIC -Wall -Wextra -Werror -pedantic \
    -I"$sdk_include" "$repo_dir/src/administration.cpp" -o "$test_dir/modules/administration.so"
objects=()
for name in storage loader native_module lua_module; do
    "${CXX:-g++}" "${flags[@]}" -I"$core_dir/src" -I"$core_dir/gen" -I"$core_dir" \
        -I/usr/include/lua5.4 -c "$core_dir/host/$name.cpp" -o "$test_dir/$name.o"
    objects+=("$test_dir/$name.o")
done
"${CXX:-g++}" "${flags[@]}" -I"$core_dir/src" -I"$core_dir/gen" -I"$core_dir" \
    "$core_dir/tests/test_administration.cpp" "${objects[@]}" "$core_dir/libkotogram.a" \
    -lssl -lcrypto -lz -lsqlite3 -ldl -llua5.4 -o "$test_dir/test_administration"
"$test_dir/test_administration" "$test_dir/modules"
