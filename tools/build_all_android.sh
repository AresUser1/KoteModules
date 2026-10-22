#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
: "${KOTO_SDK_INCLUDE:?Укажите include/ из SDK соответствующего ядра}"
: "${ANDROID_NDK_HOME:?Укажите NDK 26.3.11579264}"
tc="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
[[ -x "$tc/aarch64-linux-android26-clang" ]]
out="${KOTO_MODULE_OUTPUT_DIR:-$root/dist/android-arm64-v8a}"
mkdir -p "$out"
export KOTO_MODULE_OUTPUT_DIR="$out" KOTO_MODULE_INDEX="$out/index.json"
cp "$root/index.json" "$KOTO_MODULE_INDEX"
bash "$root/tools/build_chess.sh" --android
bash "$root/tools/build_administration.sh" --android
shopt -s nullglob
for src in "$root"/src/*.c "$root"/src/*.cpp; do
    name="$(basename -- "$src")"
    name="${name%.*}"
    case "$name" in administration|chess) continue ;; esac
    if [[ "$src" == *.cpp ]]; then compiler="$tc/aarch64-linux-android26-clang++"; standard=c++20
    else compiler="$tc/aarch64-linux-android26-clang"; standard=c11; fi
    "$compiler" -O2 -shared -fPIC -pthread "-std=$standard" -Wl,-z,nodelete -I"$KOTO_SDK_INCLUDE" \
        "$src" -o "$out/$name.so"
done
python3 - "$root" "$out" "$KOTO_SDK_INCLUDE" <<'PY'
import hashlib, json, re, subprocess, sys
from pathlib import Path
root, out, sdk = map(Path, sys.argv[1:])
catalog = json.loads((out/'index.json').read_text())
for binary in sorted(out.glob('*.so')):
    payload = binary.read_bytes()
    if payload[:6] != b'\x7fELF\x02\x01' or int.from_bytes(payload[18:20], 'little') != 183:
        raise SystemExit(f'Ожидался ELF64 AArch64: {binary.name}')
for entry in catalog['modules']:
    binary = out/f"{entry['id']}.so"
    if not binary.is_file():
        raise SystemExit(f'Не собран модуль каталога: {binary.name}')
    entry.update(size=binary.stat().st_size, sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    source = next((root/'src'/f"{entry['id']}{suffix}" for suffix in ['.cpp', '.c'] if (root/'src'/f"{entry['id']}{suffix}").exists()), None)
    if source:
        text = source.read_text()
        descriptors = list(re.finditer(r'const\s+koto_module\s+\w+\s*=', text))
        if descriptors:
            fields = re.search(r'"(\d+\.\d+\.\d+)"\s*,\s*(\d+)', text[descriptors[-1].start():])
            if fields:
                entry.update(version=fields[1], min_host_version=int(fields[2]))
(out/'index.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2)+'\n')
sha = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
info = {'repository': 'AresUser1/KoteModules', 'commit': sha, 'abi': 'arm64-v8a', 'api': 26,
        'ndk': '26.3.11579264', 'core_commit': (root/'ci/kotogram-ref').read_text().strip(),
        'sdk_header_sha256': hashlib.sha256((sdk/'module_abi.h').read_bytes()).hexdigest()}
(out/'build-info.json').write_text(json.dumps(info, indent=2)+'\n')
files = sorted(p for p in out.iterdir() if p.is_file() and p.name != 'SHA256SUMS')
(out/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files))
PY
