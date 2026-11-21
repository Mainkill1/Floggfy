#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
bash build-artwork-codecs.sh x86_64-w64-mingw32-gcc ar build/artwork-codecs-windows
# Run from WSL. Use Windows TEMP unless a destination is explicitly supplied.
if [[ $# -gt 0 ]]; then
    floggfy_test_root="$1"
    mkdir -p "$floggfy_test_root"
else
    floggfy_test_root=$(windows_test_dir Floggfy-tests)
fi
python3 tests/create_quality_fixtures.py "$floggfy_test_root"
python3 tests/create_cache_fixtures.py "$floggfy_test_root/cache-fixtures"
mkdir -p "$floggfy_test_root/cache-users/synthetic-user/primary.ldb"
cp "$floggfy_test_root/cache-fixtures/separate-album.ldb" "$floggfy_test_root/cache-users/synthetic-user/primary.ldb/000001.ldb"
mkdir -p build/windows-tests
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vendor/minimp4/minimp4.c -o build/windows-tests/minimp4.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/media/vorbis_headers_stb.c -o build/windows-tests/vorbis-headers-stb.o
python3 tools/embed_assets.py
build_windows_minhook build/windows-tests
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/windows-tests/history-ogg.o
recording_inputs=("${recording_sources[@]}" build/windows-tests/minimp4.o build/windows-tests/vorbis-headers-stb.o)
common=("${windows_cxxflags[@]}" tests/marked_catalog_stub.cpp)
x86_64-w64-mingw32-g++ "${common[@]}" -ffunction-sections -fdata-sections tests/recording_metadata_test.cpp \
 native/media/media_session.cpp -Wl,--gc-sections -lole32 -luuid -lruntimeobject -o "$floggfy_test_root/recording-metadata-test.exe"
"$floggfy_test_root/recording-metadata-test.exe"
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_history_test.cpp \
 "${recording_inputs[@]}" native/media/existing_quality.cpp native/media/file_publication.cpp native/media/metadata_update_core.cpp native/tooling/history_settings.cpp \
 native/tooling/async_log.cpp native/media/library_layout.cpp native/media/ogg_history_core.cpp native/media/image_info.cpp \
 native/media/flac_history_core.cpp native/media/compressed_buffer.cpp native/media/ogg_tags.cpp build/windows-tests/history-ogg.o \
 -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-history-test.exe"
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/windows-tests/metadata-ogg.o
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_metadata_identity_test.cpp native/telemetry/telemetry_filter.cpp native/telemetry/telemetry_decode.cpp native/telemetry/telemetry_inflate.cpp -I native/vendor/libogg/include \
 native/media/rich_metadata.cpp native/media/artwork_reply.cpp native/media/cached_metadata.cpp native/media/cached_metadata_windows.cpp native/media/cached_artwork_windows.cpp native/media/artwork_image.cpp native/tooling/history_settings.cpp native/tooling/async_log.cpp \
 native/media/media_session.cpp native/media/auto_skip.cpp native/media/library_layout.cpp native/media/album_artwork.cpp native/media/playback_quality_windows.cpp "${quality_sources[@]}" build/windows-tests/metadata-ogg.o build/windows-tests/minhook-*.o build/artwork-codecs-windows/libwebpdecoder.a -lole32 -luuid -lshell32 \
 -lruntimeobject -lwinhttp -o "$floggfy_test_root/windows-metadata-identity-test.exe"
"$floggfy_test_root/windows-history-test.exe"
"$floggfy_test_root/windows-metadata-identity-test.exe"
"$floggfy_test_root/windows-metadata-identity-test.exe" --unknown-cef
"$floggfy_test_root/windows-metadata-identity-test.exe" --missing-cef-exports

x86_64-w64-mingw32-g++ "${common[@]}" tests/cached_metadata_test.cpp native/media/cached_metadata.cpp native/media/rich_metadata.cpp -o "$floggfy_test_root/cached-metadata-test.exe"
"$floggfy_test_root/cached-metadata-test.exe" "$(wslpath -w "$floggfy_test_root/cache-fixtures")"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_cached_metadata_test.cpp native/media/cached_metadata.cpp native/media/cached_metadata_windows.cpp native/media/rich_metadata.cpp -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-cached-metadata-test.exe"
"$floggfy_test_root/windows-cached-metadata-test.exe" "$(wslpath -w "$floggfy_test_root/cache-users")"
x86_64-w64-mingw32-g++ "${common[@]}" tests/artwork_image_test.cpp native/media/artwork_image.cpp native/media/image_info.cpp build/artwork-codecs-windows/libwebpdecoder.a -o "$floggfy_test_root/artwork-image-test.exe"
"$floggfy_test_root/artwork-image-test.exe" "$(wslpath -w "$floggfy_test_root/artwork-alpha.png")"
python3 tests/check_artwork_png.py "$floggfy_test_root/artwork-alpha.png"
x86_64-w64-mingw32-g++ "${common[@]}" tests/artwork_image_allocation_test.cpp native/media/image_info.cpp build/artwork-codecs-windows/libwebpdecoder.a -o "$floggfy_test_root/artwork-image-allocation-test.exe"
"$floggfy_test_root/artwork-image-allocation-test.exe"
mkdir -p "$floggfy_test_root/artwork-cache"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_cached_artwork_test.cpp native/media/image_info.cpp native/media/artwork_image.cpp build/artwork-codecs-windows/libwebpdecoder.a -lole32 -luuid -o "$floggfy_test_root/windows-cached-artwork-test.exe"
"$floggfy_test_root/windows-cached-artwork-test.exe" "$(wslpath -w "$floggfy_test_root/artwork-cache")"

x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/windows-tests/ogg-framing.o
quality_inputs=("${quality_sources[@]}" build/windows-tests/ogg-framing.o)
x86_64-w64-mingw32-g++ "${common[@]}" tests/auto_skip_test.cpp native/media/auto_skip.cpp native/media/library_layout.cpp -o "$floggfy_test_root/auto-skip-test.exe"
"$floggfy_test_root/auto-skip-test.exe"
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/playback_quality_test.cpp "${quality_inputs[@]}" -o "$floggfy_test_root/playback-quality-test.exe"
"$floggfy_test_root/playback-quality-test.exe"
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_menu_quality_test.cpp native/media/library_layout.cpp "${quality_inputs[@]}" native/tooling/history_settings.cpp native/tooling/async_log.cpp build/windows-tests/minhook-*.o -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-menu-quality-test.exe"
"$floggfy_test_root/windows-menu-quality-test.exe" --never-pause-hidden
"$floggfy_test_root/windows-menu-quality-test.exe"

x86_64-w64-mingw32-g++ "${common[@]}" -DFLOGGFY_PUBLICATION_TEST -I native/vendor/libogg/include tests/windows_quality_observer_test.cpp "${capture_sources[@]}" build/windows-tests/ogg-framing.o build/windows-tests/minimp4.o build/windows-tests/vorbis-headers-stb.o native/media/existing_quality.cpp native/ui/song_destination.cpp native/tooling/history_settings.cpp build/windows-tests/minhook-*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_test_root/windows-quality-observer-test.exe"
"$floggfy_test_root/windows-quality-observer-test.exe"

# Synthetic host preloads System32 VERSION.dll before its entry point.
x86_64-w64-mingw32-g++ "${common[@]}" -municode tests/windows_startup_host.cpp -lws2_32 -o "$floggfy_test_root/Spotify-startup-host.exe"
x86_64-w64-mingw32-g++ "${common[@]}" -municode tests/windows_startup_test.cpp native/tooling/startup_launcher.cpp native/tooling/process_startup_loader.cpp -ladvapi32 -o "$floggfy_test_root/windows-startup-test.exe"
# Build the complete proxy so its ordinary startup readiness is tested too.
bash build-native.sh
python3 - "$floggfy_test_root" <<'PYFIXTURE'
from pathlib import Path
import shutil,sys,subprocess
root=Path(sys.argv[1]);fixture=root/'startup-fixture';fixture.mkdir(exist_ok=True)
# Remove the prior suite's automatic-mode fixture before launcher-mode tests.
(fixture/'version.dll').unlink(missing_ok=True)
shutil.copy2(root/'Spotify-startup-host.exe',fixture/'Spotify.exe')
shutil.copy2('build/version.dll',fixture/'Floggfy.dll')
location=subprocess.check_output(['wslpath','-w',str(fixture/'Media')],text=True).strip()
(fixture/'SpotifyHistory.ini').write_text('[To Disk]\nDownloads=0\nMenu=0\nMetadata=0\nLog=0\nSave Location='+location+'\n')
PYFIXTURE
"$floggfy_test_root/windows-startup-test.exe" "$(wslpath -w "$floggfy_test_root/startup-fixture")"

x86_64-w64-mingw32-g++ "${common[@]}" tests/album_artwork_test.cpp native/media/album_artwork.cpp native/media/image_info.cpp \
 native/tooling/async_log.cpp native/tooling/history_settings.cpp -lole32 -luuid -lshell32 -lwinhttp -o "$floggfy_test_root/album-artwork-test.exe"
"$floggfy_test_root/album-artwork-test.exe"

# Keep defaults fixtures separate from the other tests' explicit settings.
floggfy_settings_test_root="$floggfy_test_root/settings-defaults"
mkdir -p "$floggfy_settings_test_root"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_settings_test.cpp -lole32 -luuid -lshell32 -o "$floggfy_settings_test_root/windows-settings-test.exe"
for scenario in new missing metadata-cache-custom metadata-cache-invalid metadata-cache-zero metadata-cache-over zero legacy-zero lyrics-zero updates-zero aac-one podcasts-one podcasts-zero buffer-1024 buffer-over buffer-under auto-skip-one auto-skip-invalid auto-skip-padded never-pause-hidden never-pause-off never-pause-on never-pause-invalid never-pause-padded never-pause-duplicate speed-zero speed-10 speed-10-limit-10 speed-10-limit-over speed-2-limit-3 speed-4-limit-3 speed-10-limit-zero speed-10-limit-one speed-10-limit-invalid speed-10-limit-padded speed-invalid speed-padded fallback-one fallback-zero fallback-invalid fallback-padded fallback-blank fallback-leading-space fallback-trailing-space fallback-tab fallback-word fallback-duplicate fallback-options; do
    "$floggfy_settings_test_root/windows-settings-test.exe" "$scenario"
done

x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_settings_sections_test.cpp -lole32 -luuid -lshell32 -o "$floggfy_settings_test_root/windows-settings-sections-test.exe"
"$floggfy_settings_test_root/windows-settings-sections-test.exe"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_network_reconnect_test.cpp -o "$floggfy_test_root/windows-network-reconnect-test.exe"
"$floggfy_test_root/windows-network-reconnect-test.exe"
bash tests/runners/test-update-blocker.sh

bash tests/runners/test-artwork-policy.sh

x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_recent_downloads_test.cpp native/ui/recent_downloads.cpp -o "$floggfy_test_root/windows-recent-downloads-test.exe"
"$floggfy_test_root/windows-recent-downloads-test.exe"

x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_song_destination_test.cpp native/media/library_layout.cpp native/media/existing_quality.cpp "${recording_inputs[@]}" "${quality_inputs[@]}" native/tooling/history_settings.cpp native/tooling/async_log.cpp -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-song-destination-test.exe"
"$floggfy_test_root/windows-song-destination-test.exe"

# Unicode persistence is checked in a fresh child process, including migration.
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_settings_unicode_test.cpp -lole32 -luuid -lshell32 -o "$floggfy_settings_test_root/windows-settings-unicode-test.exe"
"$floggfy_settings_test_root/windows-settings-unicode-test.exe"

# Deliberately blocked enrichment must not delay the metadata heartbeat.
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_metadata_liveness_test.cpp native/telemetry/telemetry_filter.cpp native/telemetry/telemetry_decode.cpp native/telemetry/telemetry_inflate.cpp \
 native/media/rich_metadata.cpp native/media/artwork_reply.cpp native/media/artwork_image.cpp native/media/cached_metadata.cpp native/media/auto_skip.cpp native/media/library_layout.cpp native/tooling/history_settings.cpp native/tooling/async_log.cpp \
 native/media/media_session.cpp "${quality_sources[@]}" native/media/playback_quality_windows.cpp build/windows-tests/metadata-ogg.o build/windows-tests/minhook-*.o \
 build/artwork-codecs-windows/libwebpdecoder.a -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_test_root/windows-metadata-liveness-test.exe"
"$floggfy_test_root/windows-metadata-liveness-test.exe"
"$floggfy_test_root/windows-metadata-liveness-test.exe" --ambiguous-refresh
"$floggfy_test_root/windows-metadata-liveness-test.exe" --ambiguous-delayed
"$floggfy_test_root/windows-metadata-liveness-test.exe" --artwork-revision
"$floggfy_test_root/windows-metadata-liveness-test.exe" --fallbacks
"$floggfy_test_root/windows-metadata-liveness-test.exe" --alternate-artwork
"$floggfy_test_root/windows-metadata-liveness-test.exe" --delayed-artwork
"$floggfy_test_root/windows-metadata-liveness-test.exe" --grant-refresh

# Publication tests include real process termination and conflicting path races.
x86_64-w64-mingw32-g++ "${common[@]}" -municode -DFLOGGFY_PUBLICATION_TEST -I native/vendor/libogg/include \
 tests/windows_publication_test.cpp "${recording_inputs[@]}" native/media/existing_quality.cpp native/media/file_publication.cpp native/media/metadata_update_core.cpp native/media/library_layout.cpp "${quality_inputs[@]}" -o "$floggfy_test_root/windows-publication-test.exe"
"$floggfy_test_root/windows-publication-test.exe"

# Metadata fills work for existing FLAC and Ogg without publishing new audio.
x86_64-w64-mingw32-g++ "${common[@]}" -DFLOGGFY_PUBLICATION_TEST -I native/vendor/libogg/include \
 tests/windows_metadata_update_test.cpp native/media/metadata_update_windows.cpp native/media/metadata_update_core.cpp "${recording_inputs[@]}" native/media/existing_quality.cpp native/media/file_publication.cpp native/media/library_layout.cpp "${quality_inputs[@]}" native/tooling/history_settings.cpp native/tooling/async_log.cpp \
 -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-metadata-update-test.exe"
"$floggfy_test_root/windows-metadata-update-test.exe"

# Storage failure returns an error without dereferencing a failed file mapping.
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include \
 tests/windows_recording_read_test.cpp "${recording_inputs[@]}" native/media/library_layout.cpp "${quality_inputs[@]}" native/tooling/history_settings.cpp native/tooling/async_log.cpp \
 -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-recording-read-test.exe"
"$floggfy_test_root/windows-recording-read-test.exe"

# Focused correction regressions also remain part of the normal Windows suite.
for issue in 1 3 5; do
    bash tests/runners/test-pr11-corrections.sh "$issue"
done

# Optional AAC capture and Windows publication are exercised in one executable.
bash tests/runners/test-aac.sh

# Naming configuration and atomic conversion share one focused integration gate.
bash tests/runners/test-filename-options.sh

# Directory enumeration bounds and one-list reuse across all codec/name checks.
bash tests/runners/test-directory-lookup.sh

# Album-artist precedence remains separate from guest/contributing credits.
bash tests/runners/test-album-artists.sh

# Raw forensic input and per-stream metadata are independent of normal recording validation.
bash tests/runners/test-stream-dumps.sh

# Media-session recovery/diagnostics must not regress when publication changes.
bash tests/runners/test-media-observer.sh
bash tests/runners/test-diagnostics.sh
