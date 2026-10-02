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
python3 tests/create_cache_fixtures.py "$floggfy_test_root/cache-fixtures"
mkdir -p "$floggfy_test_root/cache-users/synthetic-user/primary.ldb"
cp "$floggfy_test_root/cache-fixtures/separate-album.ldb" "$floggfy_test_root/cache-users/synthetic-user/primary.ldb/000001.ldb"
mkdir -p build/windows-tests
python3 - <<'PY'
from pathlib import Path
script='window.__floggfyNative=true;\n'+Path('native/metadata_collector.js').read_text()
Path('build/metadata_script.h').write_text('static constexpr wchar_t metadata_script[]=LR"FLOGGFY('+script+')FLOGGFY";\n')
PY
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/windows-tests/minhook-${source//\//-}.o"
done
common=(-std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++)
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_history_test.cpp \
 native/existing_quality.cpp native/file_publication.cpp native/history_settings.cpp \
 native/async_log.cpp native/library_layout.cpp native/ogg_history_core.cpp \
 -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-history-test.exe"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_log_test.cpp native/async_log.cpp \
 native/history_settings.cpp -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-log-test.exe"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_metadata_identity_test.cpp \
 native/rich_metadata.cpp native/cached_metadata.cpp native/cached_metadata_windows.cpp native/history_settings.cpp native/async_log.cpp \
 native/media_session.cpp build/windows-tests/minhook-*.o -lole32 -luuid -lshell32 \
 -lruntimeobject -o "$floggfy_test_root/windows-metadata-identity-test.exe"
"$floggfy_test_root/windows-history-test.exe"
"$floggfy_test_root/windows-log-test.exe"
"$floggfy_test_root/windows-metadata-identity-test.exe"

x86_64-w64-mingw32-g++ "${common[@]}" tests/cached_metadata_test.cpp native/cached_metadata.cpp native/rich_metadata.cpp -o "$floggfy_test_root/cached-metadata-test.exe"
"$floggfy_test_root/cached-metadata-test.exe" "$(wslpath -w "$floggfy_test_root/cache-fixtures")"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_cached_metadata_test.cpp native/cached_metadata.cpp native/cached_metadata_windows.cpp native/rich_metadata.cpp -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-cached-metadata-test.exe"
"$floggfy_test_root/windows-cached-metadata-test.exe" "$(wslpath -w "$floggfy_test_root/cache-users")"

x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/windows-tests/ogg-framing.o
quality_sources=(native/playback_quality.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/windows-tests/ogg-framing.o)
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/playback_quality_test.cpp "${quality_sources[@]}" -o "$floggfy_test_root/playback-quality-test.exe"
"$floggfy_test_root/playback-quality-test.exe"
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_menu_quality_test.cpp "${quality_sources[@]}" native/history_settings.cpp native/async_log.cpp build/windows-tests/minhook-*.o -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-menu-quality-test.exe"
"$floggfy_test_root/windows-menu-quality-test.exe"

x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_quality_observer_test.cpp native/playback_quality_windows.cpp "${quality_sources[@]}" native/media_session.cpp native/library_layout.cpp native/existing_quality.cpp native/file_publication.cpp native/spotify_hook_discovery.cpp native/history_settings.cpp native/async_log.cpp build/windows-tests/minhook-*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_test_root/windows-quality-observer-test.exe"
"$floggfy_test_root/windows-quality-observer-test.exe"
