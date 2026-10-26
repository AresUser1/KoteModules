#!/usr/bin/env bash
set -euo pipefail

sdk="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
target="all"
source_file=""

for arg in "$@"; do
    case "$arg" in
        --host|--linux) target="host" ;;
        --all) target="all" ;;
        --arm64) target="arm64" ;;
        --arm32) target="arm32" ;;
        --help|-h)
            echo "Использование: bash build.sh [файл.c|cpp|lua] [--all|--arm64|--arm32|--host]"
            exit 0
            ;;
        *)
            [[ -f "$arg" ]] || { echo "Файл не найден: $arg" >&2; exit 2; }
            source_file="$arg"
            ;;
    esac
done

ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_HOME:-$HOME/android-toolchain/sdk}/ndk/26.3.11579264}}"
toolchain="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"

build_single() {
    local src="$1" arch="$2" compiler="$3" standard="$4" out="$5"
    local name="$(basename -- "$src")"
    local binary="$out/${name%.*}.so"
    
    mkdir -p "$out"
    if [[ "$src" == *.lua ]]; then
        if command -v luac >/dev/null; then luac -p "$src"; fi
        cp -- "$src" "$out/$name"
        echo "[LUA] $name -> $out"
        return
    fi

    echo "[BUILD] $name ($arch)..."
    "$compiler" -O2 -fPIC -shared -pthread -Wall -Wextra "-std=$standard" \
        -Wl,-z,nodelete -I"$sdk/include" "$src" -o "$binary"
    echo "  -> Готово: $binary"
}

build_for_arch() {
    local arch="$1" compiler="$2" standard_c="c11" standard_cxx="c++20" out=""
    if [[ "$arch" == "arm64-v8a" ]]; then
        out="$sdk/out/android-arm64-v8a"
        cc="$toolchain/aarch64-linux-android26-clang"
        cxx="${cc}++"
    elif [[ "$arch" == "armeabi-v7a" ]]; then
        out="$sdk/out/android-armeabi-v7a"
        cc="$toolchain/armv7a-linux-androideabi26-clang"
        cxx="${cc}++"
    else
        out="$sdk/out/host-$(uname -m)"
        cc="${CC:-cc}"
        cxx="${CXX:-c++}"
    fi

    if [[ -n "$source_file" ]]; then
        if [[ "$source_file" == *.c ]]; then
            build_single "$source_file" "$arch" "$cc" "$standard_c" "$out"
        else
            build_single "$source_file" "$arch" "$cxx" "$standard_cxx" "$out"
        fi
    else
        shopt -s nullglob
        for s in "$sdk"/src/*.c; do build_single "$s" "$arch" "$cc" "$standard_c" "$out"; done
        for s in "$sdk"/src/*.cpp "$sdk"/src/*.cc; do build_single "$s" "$arch" "$cxx" "$standard_cxx" "$out"; done
        for s in "$sdk"/src/*.lua; do build_single "$s" "$arch" "" "" "$out"; done
    fi
}

if [[ "$target" == "all" ]]; then
    build_for_arch "arm64-v8a" ""
    build_for_arch "armeabi-v7a" ""
elif [[ "$target" == "arm64" ]]; then
    build_for_arch "arm64-v8a" ""
elif [[ "$target" == "arm32" ]]; then
    build_for_arch "armeabi-v7a" ""
else
    build_for_arch "host" ""
fi

echo "Сборка завершена!"
