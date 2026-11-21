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
    -I native/vendor/libogg/include tests/windows_artwork_enrichment_test.cpp native/telemetry/telemetry_filter.cpp native/telemetry/telemetry_decode.cpp native/telemetry/telemetry_inflate.cpp \
    native/media/audio_history.cpp "${capture_sources[@]}" \
    native/media/rich_metadata.cpp native/media/artwork_reply.cpp native/media/cached_metadata.cpp native/media/cached_metadata_windows.cpp native/media/cached_artwork_windows.cpp native/media/artwork_image.cpp \
    native/media/existing_quality.cpp native/ui/song_destination.cpp \
    build/artwork-policy/*.o build/artwork-codecs-windows/libwebpdecoder.a -lole32 -luuid -lshell32 -lruntimeobject \
    -o "$floggfy_artwork_root/artwork-policy.exe"
exec "$floggfy_artwork_root/artwork-policy.exe"
