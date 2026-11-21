#!/usr/bin/env bash
# One executable for destination lookup and CEF Song target binding.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
mkdir -p build/song-shortcut
floggfy_shortcut_root=$(windows_test_dir "Floggfy-song-shortcut-$(date +%s)-$$")
python3 tests/create_quality_fixtures.py "$floggfy_shortcut_root"
build_windows_minhook build/song-shortcut
build_windows_audio_objects build/song-shortcut -Wall -Wextra
python3 tools/embed_assets.py
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" -DFLOGGFY_SONG_SHORTCUT_TEST \
    -I native/vendor/libogg/include \
    tests/windows_song_destination_test.cpp tests/windows_menu_quality_test.cpp tests/library_layout_test.cpp tests/marked_catalog_stub.cpp \
    native/media/library_layout.cpp native/media/existing_quality.cpp "${recording_sources[@]}" \
    "${quality_sources[@]}" native/tooling/history_settings.cpp native/tooling/async_log.cpp \
    build/song-shortcut/*.o -lole32 -luuid -lshell32 -o "$floggfy_shortcut_root/song-shortcut-test.exe"
exec "$floggfy_shortcut_root/song-shortcut-test.exe"
