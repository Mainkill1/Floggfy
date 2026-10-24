#!/usr/bin/env bash
# One Windows fixture for restart-only network opt-in and settings persistence.
set -euo pipefail
cd -- "$(dirname -- "$0")"
floggfy_network_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_network_root="$(wslpath -u "$floggfy_network_temp")/Floggfy-network-patch-$(date +%s)-$$"
mkdir -p "$floggfy_network_root"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode \
    -static-libgcc -static-libstdc++ tests/windows_network_patch_test.cpp native/pe_imports.cpp \
    -lole32 -luuid -lshell32 -liphlpapi -lws2_32 -o "$floggfy_network_root/network-patch-test.exe"
exec "$floggfy_network_root/network-patch-test.exe"
