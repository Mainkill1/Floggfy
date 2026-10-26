#!/usr/bin/env bash
# Focused Windows regression: no Spotify, CEF process or network traffic.
set -euo pipefail
cd -- "$(dirname -- "$0")"
floggfy_privacy_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_privacy_root="$(wslpath -u "$floggfy_privacy_temp")/Floggfy-renderer-privacy-$(date +%s)-$$"
mkdir -p "$floggfy_privacy_root"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode \
    -static-libgcc -static-libstdc++ tests/windows_renderer_privacy_test.cpp \
    -lole32 -luuid -lshell32 -o "$floggfy_privacy_root/renderer-privacy-test.exe"
exec "$floggfy_privacy_root/renderer-privacy-test.exe"
