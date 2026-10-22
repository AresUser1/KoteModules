#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_include="${KOTO_SDK_INCLUDE:-$repo_dir/../kote_module_sdk/include}"
mode="${1:---android}"
case "$mode" in
    --host)
        compiler="${CXX:-g++}"
        output="$repo_dir/build/administration/administration-linux-$(uname -m).so"
        ;;
    --android)
        ndk_dir="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_HOME:-/home/kot/android-toolchain/sdk}/ndk/26.3.11579264}}"
        compiler="$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang++"
        if [[ ! -x "$compiler" ]]; then
            echo 'Нужен Android NDK. Укажите ANDROID_NDK_HOME. Host-библиотека не заменяет Android-модуль.' >&2
            exit 1
        fi
        output="${KOTO_MODULE_OUTPUT_DIR:-$repo_dir/modules}/administration.so"
        ;;
    *) echo 'Использование: bash tools/build_administration.sh [--android|--host]' >&2; exit 2 ;;
esac
mkdir -p -- "$(dirname -- "$output")"
"$compiler" -std=c++20 -O2 -shared -fPIC -pthread -Wall -Wextra -Werror -pedantic -Wl,-z,nodelete \
    -I"$sdk_include" "$repo_dir/src/administration.cpp" -o "$output"
printf 'Собрано: %s\n' "$output"
if [[ "$mode" == --android ]]; then
    python3 - "$repo_dir" "$output" "${KOTO_MODULE_INDEX:-$repo_dir/index.json}" <<'PY'
import datetime, hashlib, json, sys
from pathlib import Path
repo,binary,index=map(Path,sys.argv[1:])
payload=binary.read_bytes()
if payload[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(payload[18:20],'little')!=183:
    raise SystemExit('Не ARM64 ELF; каталог не изменён.')
catalog=json.loads(index.read_text())
entry=next(m for m in catalog['modules'] if m['id']=='administration')
entry.update(version='1.2.0', min_host_version=60, size=len(payload), sha256=hashlib.sha256(payload).hexdigest())
catalog['updated']=datetime.datetime.now(datetime.timezone.utc).date().isoformat()
temp=index.with_suffix('.json.tmp')
temp.write_text(json.dumps(catalog,ensure_ascii=False,indent=2)+'\n')
temp.replace(index)
print('Обновлён локальный каталог. Для модуля требуется исправленное ядро versionCode 60+.')
PY
fi
