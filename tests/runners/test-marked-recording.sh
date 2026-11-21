#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh

# Use the Windows temp volume so WSL can launch the test binary directly.
windows_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
test_dir=$(mktemp -d "$(wslpath -u "$windows_temp")/Floggfy-marked-test.XXXXXXXX")
trap 'rm -rf -- "$test_dir"' EXIT
build_windows_audio_objects "$test_dir"
python3 tools/embed_assets.py
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" -I native/vendor/libogg/include \
    tests/windows_marked_recording_test.cpp native/media/marked_recording.cpp native/media/marked_recording_windows.cpp \
    native/media/metadata_update_windows.cpp native/media/metadata_update_core.cpp "${recording_sources[@]}" \
    native/media/existing_quality.cpp native/media/file_publication.cpp native/media/library_layout.cpp "${quality_sources[@]}" native/tooling/history_settings.cpp native/tooling/async_log.cpp \
    "$test_dir/minimp4.o" "$test_dir/vorbis.o" "$test_dir/ogg.o" \
    -lole32 -luuid -lshell32 -o "$test_dir/marked-recording-test.exe"
"$test_dir/marked-recording-test.exe"
