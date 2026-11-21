#!/usr/bin/env bash
# One focused Windows executable; no live Spotify or network access is needed.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
mkdir -p build/artwork-policy
bash build-artwork-codecs.sh x86_64-w64-mingw32-gcc ar build/artwork-codecs-windows
build_windows_audio_objects build/artwork-policy -Wall -Wextra
floggfy_artwork_root=$(windows_test_dir "Floggfy-artwork-policy-tests")
python3 tools/embed_assets.py
build_windows_minhook build/artwork-policy
# The test includes the actual metadata bridge and settings implementation.
# Link the capture worker too, so removed Media fields/providers cannot survive
# as unnoticed integration dependencies. Hooks and live SMTC reads are not run.
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" tests/marked_catalog_stub.cpp \
    -I native/vendor/libogg/include tests/windows_artwork_enrichment_test.cpp native/telemetry_filter.cpp native/telemetry_decode.cpp native/telemetry_inflate.cpp \
    native/audio_history.cpp "${capture_sources[@]}" \
    native/rich_metadata.cpp native/artwork_reply.cpp native/cached_metadata.cpp native/cached_metadata_windows.cpp native/cached_artwork_windows.cpp native/artwork_image.cpp \
    native/existing_quality.cpp native/song_destination.cpp \
    build/artwork-policy/*.o build/artwork-codecs-windows/libwebpdecoder.a -lole32 -luuid -lshell32 -lruntimeobject \
    -o "$floggfy_artwork_root/artwork-policy.exe"
exec "$floggfy_artwork_root/artwork-policy.exe"
