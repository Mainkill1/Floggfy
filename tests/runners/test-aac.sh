#!/usr/bin/env bash
# One Windows executable for AAC capture/publication and the media I/O boundary.
# Optional arguments: a complete inspection SpAC file and the Spotify DLL to map
# for static capability discovery. Neither input is bundled with the project.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
if [[ $# != 0 && $# != 2 ]]; then echo 'Usage: bash tests/runners/test-aac.sh [SPAC_FILE SPOTIFY_DLL]' >&2; exit 2; fi
mkdir -p build/aac-tests
floggfy_aac_root=$(windows_test_dir "Floggfy-aac-tests-$(date +%s)-$$")
build_windows_minhook build/aac-tests
build_windows_audio_objects build/aac-tests -Wall -Wextra
python3 tools/embed_assets.py
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" -DFLOGGFY_COMBINED_TEST -municode \
    -I native/vendor/libogg/include \
    tests/windows_aac_capture_test.cpp tests/spac_stream_test.cpp tests/aac_container_test.cpp tests/windows_media_boundary_test.cpp tests/marked_catalog_stub.cpp \
    "${capture_sources[@]}" native/media/existing_quality.cpp native/ui/song_destination.cpp \
    build/aac-tests/*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_aac_root/aac-test.exe"
if [[ $# == 2 ]]; then
    "$floggfy_aac_root/aac-test.exe" "$(wslpath -w "$1")" "$(wslpath -w "$floggfy_aac_root/source-sample.m4a")" "$(wslpath -w "$2")"
    echo "M4A inspection output: $floggfy_aac_root/source-sample.m4a"
else
    "$floggfy_aac_root/aac-test.exe"
fi
