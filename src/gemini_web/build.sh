#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ecosystem_dir="${GWEB_ECOSYSTEM_DIR:-$project_dir/../../codex-fixes}"
sdk_include="${KOTO_SDK_INCLUDE:-$ecosystem_dir/KoteModules_repo/sdk/include}"
openssl_android="${GWEB_ANDROID_OPENSSL:-$ecosystem_dir/kotogram/kotogram-apk/app/src/main/cpp/third_party/arm64-v8a}"
ndk_dir="${ANDROID_NDK_HOME:-$HOME/android-toolchain/sdk/ndk/26.3.11579264}"
mode="${1:---android}"
mkdir -p "$project_dir/build"
sources=("$project_dir/https.cpp" "$project_dir/client.cpp" "$project_dir/module.cpp")
common=(-std=c++20 -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections
        -pthread -Wall -Wextra -Wpedantic -I"$sdk_include")
case "$mode" in
    --android)
        toolchain="$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64/bin"
        "$toolchain/aarch64-linux-android26-clang++" "${common[@]}" -shared -static-libstdc++ \
            -I"$openssl_android/include" "${sources[@]}" \
            "$openssl_android/lib/libssl.a" "$openssl_android/lib/libcrypto.a" \
            -lz -ldl -Wl,-z,defs -Wl,-z,max-page-size=16384 -Wl,--gc-sections \
            -Wl,--exclude-libs,ALL -Wl,-Bsymbolic -Wl,--version-script="$project_dir/exports.map" \
            -o "$project_dir/build/gemini_web.so"
        "$toolchain/llvm-strip" --strip-unneeded "$project_dir/build/gemini_web.so"
        "$toolchain/llvm-readelf" -h "$project_dir/build/gemini_web.so"
        "$toolchain/llvm-readelf" -d "$project_dir/build/gemini_web.so" | rg NEEDED
        python3 "$project_dir/verify_elf.py" "$project_dir/build/gemini_web.so" "$toolchain/llvm-readelf"
        ;;
    --host)
        "${CXX:-c++}" "${common[@]}" -shared "${sources[@]}" -lssl -lcrypto -lz \
            -Wl,-z,defs -Wl,--gc-sections -Wl,--version-script="$project_dir/exports.map" \
            -o "$project_dir/build/gemini_web-host.so"
        ;;
    --test|--sanitize)
        extra=(-O0 -g)
        if [[ "$mode" == --sanitize ]]; then extra=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer); fi
        "${CXX:-c++}" "${common[@]}" "${extra[@]}" -DGWEB_TESTING \
            "${sources[@]}" "$project_dir/tests.cpp" -lssl -lcrypto -lz -o "$project_dir/build/tests"
        if [[ "$mode" == --sanitize ]]; then
            # LeakSanitizer cannot trace threads in the managed build sandbox.
            # AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.
            ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" \
                "$project_dir/build/tests"
        else
            "$project_dir/build/tests"
        fi
        ;;
    *) echo 'Usage: bash build.sh [--android|--host|--test|--sanitize]' >&2; exit 2 ;;
esac
