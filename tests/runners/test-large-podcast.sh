#!/usr/bin/env bash
# Optional Windows large-file gate: ~1.1 GiB RAM and 513 MiB temporary disk.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
mkdir -p build/large-podcast
floggfy_large_root=$(windows_test_dir "Floggfy-large-podcast-test-$(date +%s)-$$")
build_windows_audio_objects build/large-podcast
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" \
    -I native/vendor/libogg/include tests/windows_large_podcast_test.cpp native/file_publication.cpp \
    native/existing_quality.cpp native/metadata_update_core.cpp "${recording_sources[@]}" \
    native/library_layout.cpp "${quality_sources[@]}" build/large-podcast/*.o \
    -o "$floggfy_large_root/test.exe"
exec "$floggfy_large_root/test.exe"
