#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
cxx=${CXX:-x86_64-w64-mingw32-g++}
floggfy_temp_win=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_test_dir=$(mktemp -d "$(wslpath -u "$floggfy_temp_win")/floggfy-update-test.XXXXXX")
trap 'rm -rf "$floggfy_test_dir"' EXIT
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -static -static-libgcc -static-libstdc++ \
    tests/windows_update_blocker_test.cpp -ladvapi32 -lole32 -lshell32 -luuid \
    -o "$floggfy_test_dir/test.exe"
"$floggfy_test_dir/test.exe"
