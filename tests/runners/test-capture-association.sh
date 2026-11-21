#!/usr/bin/env bash
# One focused event-ordering test. Does not launch Spotify or write music files.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
mkdir -p build/capture-association
build_windows_audio_objects build/capture-association -Wall -Wextra
floggfy_association_root=$(windows_test_dir "Floggfy-association-tests")
build_windows_minhook build/capture-association
python3 tools/embed_assets.py
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" tests/marked_catalog_stub.cpp \
    -I native/vendor/libogg/include tests/windows_quality_observer_test.cpp \
    "${capture_sources[@]}" native/media/existing_quality.cpp native/ui/song_destination.cpp native/tooling/history_settings.cpp \
    build/capture-association/*.o -lole32 -luuid -lshell32 -lruntimeobject \
    -o "$floggfy_association_root/capture-association.exe"
exec "$floggfy_association_root/capture-association.exe" --association
