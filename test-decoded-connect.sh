#!/usr/bin/env bash
# One focused Windows executable; static mapping never executes Spotify's DLL.
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ $# != 1 ]]; then echo 'Usage: bash test-decoded-connect.sh SPOTIFY_DLL' >&2; exit 2; fi
mkdir -p build/decoded-connect-tests
floggfy_decoded_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_decoded_root="$(wslpath -u "$floggfy_decoded_temp")/Floggfy-decoded-connect-$(date +%s)-$$"
mkdir -p "$floggfy_decoded_root"
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/decoded-connect-tests/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode -DFLOGGFY_COMBINED_TEST \
    -static-libgcc -static-libstdc++ tests/windows_decoded_connect_test.cpp \
    tests/connect_message_test.cpp tests/connect_hook_discovery_test.cpp \
    native/connect_message.cpp native/spotify_hook_discovery.cpp build/decoded-connect-tests/*.o \
    -lole32 -luuid -lshell32 -o "$floggfy_decoded_root/decoded-connect-test.exe"
exec "$floggfy_decoded_root/decoded-connect-test.exe" "$(wslpath -w "$1")"
