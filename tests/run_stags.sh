#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk="${KOTO_SDK_INCLUDE:-$root/sdk/include}"
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
flags=(-std=c++20 -O1 -g0 -pthread)
if [[ "${KOTO_SANITIZE:-0}" == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-g++}" "${flags[@]}" -shared -fPIC -I"$sdk" "$root/src/stags.cpp" -o "$tmp/stags.so"
"${CXX:-g++}" "${flags[@]}" -I"$sdk" "$root/tests/test_stags.cpp" -ldl -o "$tmp/test_stags"
"$tmp/test_stags" "$tmp/stags.so"
