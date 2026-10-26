#!/usr/bin/env bash
# One combined targeted gate; no real Spotify process, playback or network calls.
set -euo pipefail
cd -- "$(dirname -- "$0")"
floggfy_deep_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_deep_root="$(wslpath -u "$floggfy_deep_temp")/Floggfy-deep-diagnostics-$(date +%s)-$$"
mkdir -p "$floggfy_deep_root"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode \
    -static-libgcc -static-libstdc++ tests/windows_deep_diagnostics_test.cpp \
    native/startup_launcher.cpp -lole32 -luuid -lshell32 -o "$floggfy_deep_root/deep-diagnostics-test.exe"
exec "$floggfy_deep_root/deep-diagnostics-test.exe"
