#!/usr/bin/env bash
# One combined targeted execution. No live Spotify commands or network traffic.
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ $# != 1 ]]; then echo 'Usage: bash test-networking-log.sh SPOTIFY_DLL' >&2; exit 2; fi
floggfy_network_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_network_root="$(wslpath -u "$floggfy_network_temp")/Floggfy-networking-test-$(date +%s)-$$"
mkdir -p "$floggfy_network_root/logging" "$floggfy_network_root/decoded" build/networking-tests
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode \
    -static-libgcc -static-libstdc++ tests/windows_networking_log_test.cpp \
    -lole32 -luuid -lshell32 -o "$floggfy_network_root/logging/networking-log-test.exe"
"$floggfy_network_root/logging/networking-log-test.exe"
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/networking-tests/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode -DFLOGGFY_COMBINED_TEST \
    -static-libgcc -static-libstdc++ tests/windows_decoded_connect_test.cpp \
    tests/connect_message_test.cpp tests/connect_hook_discovery_test.cpp \
    native/connect_message.cpp native/spotify_hook_discovery.cpp build/networking-tests/*.o \
    -lole32 -luuid -lshell32 -o "$floggfy_network_root/decoded/decoded-connect-test.exe"
"$floggfy_network_root/decoded/decoded-connect-test.exe" "$(wslpath -w "$1")"
node tests/collector_networking_test.js
node tests/collector_connect_test.js
node tests/collector_debug_test.js
