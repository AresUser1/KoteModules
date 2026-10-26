#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export KOTO_SDK_INCLUDE="$root/sdk/include"

mkdir -p "$root/build"
g++ -O2 -shared -fPIC -std=c++20 -I"$KOTO_SDK_INCLUDE" \
    "$root/src/weather.cpp" -o "$root/build/libweather_host.so"

g++ -O2 -std=c++20 -I"$KOTO_SDK_INCLUDE" \
    "$root/tests/test_weather.cpp" -ldl -o "$root/build/test_weather"

"$root/build/test_weather" "$root/build/libweather_host.so"
