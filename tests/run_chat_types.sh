#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
core="${KOTO_CORE_DIR:-$root/../kotogram/kotogram-lib}"
sdk="${KOTO_SDK_INCLUDE:-$core/../kote_module_sdk/include}"
temp="$(mktemp -d)"
trap 'rm -rf -- "$temp"' EXIT
mkdir "$temp/modules"
for name in afk autoresponder moderation mass_notifier rp stags spam_ban banall; do
    "${CXX:-g++}" -std=c++20 -O0 -g0 -pthread -shared -fPIC -I"$sdk" \
        "$root/src/$name.cpp" -o "$temp/modules/$name.so"
done
sources=()
for name in storage loader native_module lua_module; do sources+=("$core/host/$name.cpp"); done
"${CXX:-g++}" -std=c++20 -O0 -g0 -pthread -I"$core/src" -I"$core/gen" -I"$core" \
    -I/usr/include/lua5.4 "$core/tests/test_chat_types.cpp" "${sources[@]}" "$core/libkotogram.a" \
    -lssl -lcrypto -lz -lsqlite3 -ldl -llua5.4 -o "$temp/test_chat_types"
"$temp/test_chat_types" "$temp/modules"
