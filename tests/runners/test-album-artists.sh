#!/usr/bin/env bash
# One focused invocation for album-artist precedence and legacy folder reuse.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
mkdir -p build/album-artists
# The host archiver packages the cross-compiled objects, as in the Windows umbrella.
bash build-artwork-codecs.sh x86_64-w64-mingw32-gcc ar build/artwork-codecs-windows
python3 tools/embed_assets.py
floggfy_album_root=$(windows_test_dir "Floggfy-album-artists-$(date +%s)-$$")
build_windows_minhook build/album-artists
build_windows_audio_objects build/album-artists -Wall -Wextra
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" tests/marked_catalog_stub.cpp -municode \
    -I native/vendor/libogg/include tests/windows_album_artist_test.cpp native/telemetry_filter.cpp native/telemetry_decode.cpp native/telemetry_inflate.cpp \
    native/rich_metadata.cpp native/artwork_reply.cpp native/cached_metadata.cpp native/async_log.cpp native/media_session.cpp \
    "${quality_sources[@]}" native/artwork_image.cpp native/playback_quality_windows.cpp \
    native/auto_skip.cpp native/library_layout.cpp \
    native/existing_quality.cpp "${recording_sources[@]}" native/file_publication.cpp native/metadata_update_core.cpp \
    build/album-artists/*.o build/artwork-codecs-windows/libwebpdecoder.a -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_album_root/album-artist-test.exe"
node tests/album_artist_collector_test.js
exec "$floggfy_album_root/album-artist-test.exe"
