#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk="${KOTO_SDK_INCLUDE:-$root/sdk/include}"
temp="$(mktemp -d)"
trap 'rm -rf -- "$temp"' EXIT
"${CXX:-g++}" -std=c++20 -O1 -g0 -shared -fPIC -pthread -I"$sdk" "$root/src/banall.cpp" -o "$temp/banall.so"
"${CXX:-g++}" -std=c++20 -O1 -g0 -I"$sdk" "$root/tests/test_banall.cpp" -ldl -o "$temp/test_banall"
"$temp/test_banall" "$temp/banall.so"
