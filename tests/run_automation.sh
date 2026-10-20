#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk="${KOTO_SDK_INCLUDE:-$root/sdk/include}"
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
"${CC:-cc}" -std=c11 -O1 -pthread -shared -fPIC -I"$sdk" "$root/src/auto_manager.c" -o "$tmp/auto_manager.so"
"${CXX:-g++}" -std=c++20 -O1 -pthread -shared -fPIC -I"$sdk" "$root/src/chat.cpp" -o "$tmp/chat.so"
"${CXX:-g++}" -std=c++20 -O1 -I"$sdk" "$root/tests/test_automation.cpp" -ldl -o "$tmp/test_automation"
"$tmp/test_automation" "$tmp/auto_manager.so" "$tmp/chat.so"
