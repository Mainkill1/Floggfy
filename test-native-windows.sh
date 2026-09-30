#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
# Run from WSL. Use Windows TEMP unless a destination is explicitly supplied.
if [[ $# -gt 0 ]]; then
    floggfy_test_root="$1"
else
    floggfy_temp_win=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
    floggfy_test_root="$(wslpath -u "$floggfy_temp_win")/Floggfy-tests"
fi
mkdir -p "$floggfy_test_root"
python3 tests/create_quality_fixtures.py "$floggfy_test_root"
cp SpotifyHistory.ini.example "$floggfy_test_root/SpotifyHistory.ini"
common=(-std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++)
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_history_test.cpp \
 native/existing_quality.cpp native/file_publication.cpp native/history_settings.cpp \
 native/async_log.cpp native/library_layout.cpp native/ogg_history_core.cpp \
 -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-history-test.exe"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_log_test.cpp native/async_log.cpp \
 native/history_settings.cpp -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-log-test.exe"
"$floggfy_test_root/windows-history-test.exe"
"$floggfy_test_root/windows-log-test.exe"
