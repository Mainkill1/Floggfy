#!/usr/bin/env bash
# One combined targeted gate for the three review findings and discovery refactor.
# No live Spotify execution, playback commands or network traffic.
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ $# != 1 ]]; then echo 'Usage: bash test-pr13-review.sh SPOTIFY_DLL' >&2; exit 2; fi
floggfy_review_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_review_root="$(wslpath -u "$floggfy_review_temp")/Floggfy-pr13-review-$(date +%s)-$$"
mkdir -p "$floggfy_review_root"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode \
    -static-libgcc -static-libstdc++ tests/windows_pr13_settings_test.cpp \
    -lole32 -luuid -lshell32 -o "$floggfy_review_root/review-settings-test.exe"
"$floggfy_review_root/review-settings-test.exe"
bash test-playback-diagnostics.sh
bash test-decoded-connect.sh "$1"
