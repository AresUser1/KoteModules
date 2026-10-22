#!/usr/bin/env bash
# Explicit targets: a Linux test module must never enter the Android catalog.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_include="${KOTO_SDK_INCLUDE:-$repo_dir/../kote_module_sdk/include}"
mode="${1:---android}"
if [[ ! -f "$sdk_include/module_abi.h" ]]; then
    echo 'Не найден module_abi.h. Укажите KOTO_SDK_INCLUDE=/путь/к/sdk/include.' >&2
    exit 1
fi
case "$mode" in
    --host)
        compiler="${CC:-cc}"
        output="$repo_dir/build/chess/chess-linux-$(uname -m).so"
        ;;
    --android)
        ndk_dir="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
        if [[ -z "$ndk_dir" ]]; then
            ndk_dir="${ANDROID_HOME:-/home/kot/android-toolchain/sdk}/ndk/26.3.11579264"
        fi
        compiler="$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
        if [[ ! -x "$compiler" ]]; then
            echo 'Нужен Android NDK. Укажите ANDROID_NDK_HOME и повторите --android.' >&2
            echo 'Linux-бинарник вместо Android-модуля создаваться не будет.' >&2
            exit 1
        fi
        output="${KOTO_MODULE_OUTPUT_DIR:-$repo_dir/modules}/chess.so"
        ;;
    *) echo 'Использование: bash tools/build_chess.sh [--android|--host]' >&2; exit 2 ;;
esac
mkdir -p -- "$(dirname -- "$output")"
"$compiler" -std=c11 -O2 -shared -fPIC -pthread -Wall -Wextra -Werror -pedantic -Wl,-z,nodelete \
    -I"$sdk_include" "$repo_dir/src/chess.c" -o "$output"
printf 'Собрано: %s\n' "$output"
if [[ "$mode" == --android ]]; then
    # Register only a real ARM64 artifact, never a source-only placeholder.
    python3 - "$repo_dir" "$output" "${KOTO_MODULE_INDEX:-$repo_dir/index.json}" <<'PY'
import datetime
import hashlib
import json
from pathlib import Path
import sys

repo, binary, index = map(Path, sys.argv[1:])
payload = binary.read_bytes()
if payload[:6] != b'\x7fELF\x02\x01' or int.from_bytes(payload[18:20], 'little') != 183:
    raise SystemExit('Файл не является ELF64 AArch64; каталог не изменён.')
catalog = json.loads(index.read_text())
entry = {
    'id': 'chess', 'name': 'chess', 'display_name': 'Шахматы', 'author': 'Kote',
    'description': 'Шахматы PvP на кнопках: рокировка, взятие на проходе, превращение, мат, ничьи, реванш и обычные Unicode-фигуры.',
    'version': '1.0.0', 'kind': 'so', 'abi': 'arm64-v8a',
    'commands': [
        {'name': 'chess', 'doc': 'Создать шахматный вызов', 'usage': '[white|black]'},
        {'name': 'шахматы', 'doc': 'Создать шахматный вызов', 'usage': '[white|black]'},
        {'name': 'chessend', 'doc': 'Партии в чате / закрыть свою', 'usage': '[id]'},
        {'name': 'chesshelp', 'doc': 'Управление и правила', 'usage': ''},
    ],
    'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(),
    'url': 'https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/chess.so',
    'git_url': 'https://github.com/AresUser1/KoteModules/blob/main/src/chess.c',
    'license': 'MIT', 'min_host_version': 56,
}
modules = catalog['modules']
for i, old in enumerate(modules):
    if old['id'] == 'chess':
        modules[i] = entry
        break
else:
    at = next((i for i, old in enumerate(modules) if old['id'] > 'chess'), len(modules))
    modules.insert(at, entry)
catalog['updated'] = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
temporary = index.with_suffix('.json.tmp')
temporary.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n')
temporary.replace(index)
print('Локальный index.json обновлён. Публикация не выполнялась.')
PY
fi
