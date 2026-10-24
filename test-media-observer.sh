#!/usr/bin/env bash
# One focused Windows test of timeline continuity and the media I/O boundary.
set -euo pipefail
cd -- "$(dirname -- "$0")"
floggfy_media_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_media_root="$(wslpath -u "$floggfy_media_temp")/Floggfy-media-tests-$(date +%s)-$$"
mkdir -p "$floggfy_media_root"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
    tests/windows_media_boundary_test.cpp native/media_session.cpp native/ogg_history_core.cpp \
    -lole32 -luuid -lruntimeobject -o "$floggfy_media_root/media-test.exe"
exec "$floggfy_media_root/media-test.exe"
