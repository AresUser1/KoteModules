#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_include="${KOTO_SDK_INCLUDE:-$repo_dir/../kote_module_sdk/include}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/kotobas-chess-test.XXXXXX")"
trap 'rm -rf -- "$test_dir"' EXIT
flags=(-std=c11 -O2 -pthread -Wall -Wextra -Werror -pedantic)
if [[ "${1:-}" == --sanitize ]]; then
    flags+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
elif [[ $# -gt 0 ]]; then
    echo 'Использование: bash tests/run_chess.sh [--sanitize]' >&2; exit 2
fi
"${CC:-cc}" "${flags[@]}" -I"$sdk_include" "$repo_dir/tests/test_chess.c" -o "$test_dir/test_chess"
"$test_dir/test_chess"
