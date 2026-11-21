#!/usr/bin/env bash
# One executable for filename settings, safe renames and track-start integration.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
mkdir -p build/filename-options
floggfy_names_root=$(windows_test_dir "Floggfy-filename-options-$(date +%s)-$$")
build_windows_minhook build/filename-options
build_windows_audio_objects build/filename-options -Wall -Wextra
python3 tools/embed_assets.py
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" -municode \
    -I native/vendor/libogg/include tests/windows_filename_options_test.cpp tests/marked_catalog_stub.cpp \
    "${capture_sources[@]}" native/media/existing_quality.cpp native/ui/song_destination.cpp \
    build/filename-options/*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_names_root/filename-options-test.exe"
exec "$floggfy_names_root/filename-options-test.exe"
