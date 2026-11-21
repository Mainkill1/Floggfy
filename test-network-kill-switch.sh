#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
cxx=${CXX:-x86_64-w64-mingw32-g++}
cc=${CC:-x86_64-w64-mingw32-gcc}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
for source in buffer hook trampoline hde/hde64; do
 "$cc" -O2 -I native/vendor/minhook/include -I native/vendor/minhook/src -c "native/vendor/minhook/src/$source.c" -o "$build/${source##*/}.o"
done
"$cxx" -std=c++17 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ -I native tests/windows_network_kill_switch_test.cpp native/network_kill_switch.cpp "$build"/*.o -Wl,--wrap=__imp_MapViewOfFile -lws2_32 -o "$build/test.exe"
# A unique directory keeps the helper's test INI separate from Spotify's INI.
windows_dir=$(mktemp -d /mnt/c/Users/main/AppData/Local/Temp/floggfy-network-test.XXXXXX)
trap 'rm -rf "$build" "$windows_dir"' EXIT
cp "$build/test.exe" "$windows_dir/test.exe"
"$windows_dir/test.exe" --helper-race-enable
"$windows_dir/test.exe" --helper-race-disable
"$windows_dir/test.exe" --helper-first-config
"$windows_dir/test.exe" --hidden
"$windows_dir/test.exe" --failure
"$windows_dir/test.exe"
