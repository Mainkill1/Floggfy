#!/usr/bin/env bash
# One focused gate for Connect debug setting and passive client observations.
set -euo pipefail
cd -- "$(dirname -- "$0")"
floggfy_connect_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_connect_root="$(wslpath -u "$floggfy_connect_temp")/Floggfy-connect-debug-$(date +%s)-$$"
mkdir -p "$floggfy_connect_root"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode \
    -static-libgcc -static-libstdc++ tests/windows_connect_settings_test.cpp \
    -lole32 -luuid -lshell32 -o "$floggfy_connect_root/connect-settings-test.exe"
"$floggfy_connect_root/connect-settings-test.exe"
node tests/collector_connect_test.js
