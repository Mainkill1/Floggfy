#!/usr/bin/env bash
# Read-only discovery matrix for signed/local Spotify client DLLs.
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ $# == 0 ]]; then
    echo 'Usage: bash test-client-compatibility.sh SPOTIFY_DLL [...]' >&2
    exit 2
fi
floggfy_temp_win=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_matrix_root="$(wslpath -u "$floggfy_temp_win")/Floggfy-client-matrix-$(date +%s)-$$"
mkdir -p "$floggfy_matrix_root"
matrix="$floggfy_matrix_root/discovery.exe"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode \
    -static-libgcc -static-libstdc++ tests/windows_decoded_output_discovery_test.cpp \
    native/spotify_hook_discovery.cpp -lole32 -luuid -lshell32 -o "$matrix"
for dll in "$@"; do
    if [[ ! -f "$dll" ]]; then
        echo "Missing Spotify DLL: $dll" >&2
        exit 2
    fi
    printf 'client=%s ' "$(basename -- "$(dirname -- "$dll")")"
    timeout --signal=INT --kill-after=5s 20s \
        "$matrix" --probe "$(wslpath -w "$dll")" </dev/null
done
