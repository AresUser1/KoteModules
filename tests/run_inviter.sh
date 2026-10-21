#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

so="$tmp_dir/inviter.so"
test_bin="$tmp_dir/test_inviter"

cxx="${CXX:-g++}"

"$cxx" -std=c++20 -O2 -fPIC -shared \
    -I"$repo_root/sdk/include" \
    "$repo_root/src/inviter.cpp" \
    -o "$so"

"$cxx" -std=c++20 -O2 \
    -I"$repo_root/sdk/include" \
    "$repo_root/tests/test_inviter.cpp" \
    -ldl \
    -o "$test_bin"

"$test_bin" "$so"
