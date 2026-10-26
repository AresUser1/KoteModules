#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
"${CXX:-g++}" -std=c++20 -O1 -pthread -I"${KOTO_SDK_INCLUDE:-$root/sdk/include}" "$root/tests/test_dox.cpp" -o "$tmp/test_dox"
"$tmp/test_dox"
