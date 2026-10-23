#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
# Run from WSL. Use Windows TEMP unless a destination is explicitly supplied.
if [[ $# -gt 0 ]]; then
    floggfy_test_root="$1"
else
    floggfy_temp_win=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
    floggfy_test_root="$(wslpath -u "$floggfy_temp_win")/Floggfy-tests"
fi
mkdir -p "$floggfy_test_root"
python3 tests/create_quality_fixtures.py "$floggfy_test_root"
python3 tests/create_cache_fixtures.py "$floggfy_test_root/cache-fixtures"
mkdir -p "$floggfy_test_root/cache-users/synthetic-user/primary.ldb"
cp "$floggfy_test_root/cache-fixtures/separate-album.ldb" "$floggfy_test_root/cache-users/synthetic-user/primary.ldb/000001.ldb"
mkdir -p build/windows-tests
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vorbis_headers_stb.c -o build/windows-tests/vorbis-headers-stb.o
python3 - <<'PY'
from pathlib import Path
script='window.__floggfyNative=true;\n'+Path('native/metadata_collector.js').read_text()
Path('build/metadata_script.h').write_text('static constexpr wchar_t metadata_script[]=LR"FLOGGFY('+script+')FLOGGFY";\n')
PY
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/windows-tests/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/windows-tests/history-ogg.o
common=(-std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++)
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_history_test.cpp \
 native/recording_file.cpp native/vorbis_headers.cpp build/windows-tests/vorbis-headers-stb.o native/existing_quality.cpp native/file_publication.cpp native/metadata_update_core.cpp native/history_settings.cpp \
 native/async_log.cpp native/library_layout.cpp native/ogg_history_core.cpp native/image_info.cpp \
 native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp build/windows-tests/history-ogg.o \
 -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-history-test.exe"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_log_test.cpp native/async_log.cpp \
 native/history_settings.cpp -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-log-test.exe"
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/windows-tests/metadata-ogg.o
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_metadata_identity_test.cpp -I native/vendor/libogg/include \
 native/rich_metadata.cpp native/cached_metadata.cpp native/cached_metadata_windows.cpp native/history_settings.cpp native/async_log.cpp \
 native/media_session.cpp native/thumbnail_cache.cpp native/album_artwork.cpp native/playback_quality_windows.cpp native/playback_quality.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/image_info.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/windows-tests/metadata-ogg.o build/windows-tests/minhook-*.o -lole32 -luuid -lshell32 \
 -lruntimeobject -lwinhttp -o "$floggfy_test_root/windows-metadata-identity-test.exe"
"$floggfy_test_root/windows-history-test.exe"
"$floggfy_test_root/windows-log-test.exe"
"$floggfy_test_root/windows-metadata-identity-test.exe"

x86_64-w64-mingw32-g++ "${common[@]}" tests/cached_metadata_test.cpp native/cached_metadata.cpp native/rich_metadata.cpp -o "$floggfy_test_root/cached-metadata-test.exe"
"$floggfy_test_root/cached-metadata-test.exe" "$(wslpath -w "$floggfy_test_root/cache-fixtures")"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_cached_metadata_test.cpp native/cached_metadata.cpp native/cached_metadata_windows.cpp native/rich_metadata.cpp -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-cached-metadata-test.exe"
"$floggfy_test_root/windows-cached-metadata-test.exe" "$(wslpath -w "$floggfy_test_root/cache-users")"

x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/windows-tests/ogg-framing.o
quality_sources=(native/playback_quality.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/image_info.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/windows-tests/ogg-framing.o)
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/playback_quality_test.cpp "${quality_sources[@]}" -o "$floggfy_test_root/playback-quality-test.exe"
"$floggfy_test_root/playback-quality-test.exe"
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_menu_quality_test.cpp native/library_layout.cpp "${quality_sources[@]}" native/history_settings.cpp native/async_log.cpp build/windows-tests/minhook-*.o -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-menu-quality-test.exe"
"$floggfy_test_root/windows-menu-quality-test.exe"

x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_quality_observer_test.cpp native/playback_quality_windows.cpp "${quality_sources[@]}" native/media_session.cpp native/thumbnail_cache.cpp native/library_layout.cpp native/recording_file.cpp native/vorbis_headers.cpp build/windows-tests/vorbis-headers-stb.o native/existing_quality.cpp native/file_publication.cpp native/lyrics_update.cpp native/metadata_update_core.cpp native/metadata_update_windows.cpp native/recent_downloads.cpp native/song_destination.cpp native/spotify_hook_discovery.cpp native/history_settings.cpp native/async_log.cpp build/windows-tests/minhook-*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_test_root/windows-quality-observer-test.exe"
"$floggfy_test_root/windows-quality-observer-test.exe"

x86_64-w64-mingw32-g++ "${common[@]}" -DFLOGGFY_PUBLICATION_TEST -I native/vendor/libogg/include tests/windows_lyrics_update_test.cpp native/lyrics_update.cpp native/recording_file.cpp native/vorbis_headers.cpp build/windows-tests/vorbis-headers-stb.o native/existing_quality.cpp native/file_publication.cpp native/metadata_update_core.cpp native/library_layout.cpp "${quality_sources[@]}" native/history_settings.cpp native/async_log.cpp -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-lyrics-update-test.exe"
"$floggfy_test_root/windows-lyrics-update-test.exe"

# Synthetic host preloads System32 VERSION.dll before its entry point.
x86_64-w64-mingw32-g++ "${common[@]}" -municode tests/windows_startup_host.cpp -o "$floggfy_test_root/Spotify-startup-host.exe"
x86_64-w64-mingw32-g++ "${common[@]}" -municode tests/windows_startup_test.cpp native/startup_launcher.cpp -ladvapi32 -o "$floggfy_test_root/windows-startup-test.exe"
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

x86_64-w64-mingw32-g++ "${common[@]}" tests/album_artwork_test.cpp native/album_artwork.cpp native/image_info.cpp -lwinhttp -o "$floggfy_test_root/album-artwork-test.exe"
"$floggfy_test_root/album-artwork-test.exe"

# Keep defaults fixtures separate from the other tests' explicit settings.
floggfy_settings_test_root="$floggfy_test_root/settings-defaults"
mkdir -p "$floggfy_settings_test_root"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_settings_test.cpp -lole32 -luuid -lshell32 -o "$floggfy_settings_test_root/windows-settings-test.exe"
for scenario in new missing zero legacy-zero lyrics-zero updates-zero; do
    "$floggfy_settings_test_root/windows-settings-test.exe" "$scenario"
done

x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_artwork_enrichment_test.cpp \
 native/rich_metadata.cpp native/cached_metadata.cpp native/cached_metadata_windows.cpp native/history_settings.cpp native/async_log.cpp \
 native/media_session.cpp native/thumbnail_cache.cpp native/image_info.cpp native/playback_quality_windows.cpp native/playback_quality.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/windows-tests/metadata-ogg.o build/windows-tests/minhook-*.o \
 -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_test_root/windows-artwork-enrichment-test.exe"
"$floggfy_test_root/windows-artwork-enrichment-test.exe"

x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_recent_downloads_test.cpp native/recent_downloads.cpp -o "$floggfy_test_root/windows-recent-downloads-test.exe"
"$floggfy_test_root/windows-recent-downloads-test.exe"

x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_song_destination_test.cpp native/library_layout.cpp native/existing_quality.cpp native/recording_file.cpp native/vorbis_headers.cpp build/windows-tests/vorbis-headers-stb.o "${quality_sources[@]}" native/history_settings.cpp native/async_log.cpp -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-song-destination-test.exe"
"$floggfy_test_root/windows-song-destination-test.exe"

# Unicode persistence is checked in a fresh child process, including migration.
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_settings_unicode_test.cpp -lole32 -luuid -lshell32 -o "$floggfy_settings_test_root/windows-settings-unicode-test.exe"
"$floggfy_settings_test_root/windows-settings-unicode-test.exe"

# Deliberately blocked enrichment must not delay the metadata heartbeat.
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_metadata_liveness_test.cpp \
 native/rich_metadata.cpp native/cached_metadata.cpp native/history_settings.cpp native/async_log.cpp \
 native/media_session.cpp native/thumbnail_cache.cpp native/image_info.cpp native/playback_quality_windows.cpp native/playback_quality.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/windows-tests/metadata-ogg.o build/windows-tests/minhook-*.o \
 -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_test_root/windows-metadata-liveness-test.exe"
"$floggfy_test_root/windows-metadata-liveness-test.exe"

# Publication tests include real process termination and conflicting path races.
x86_64-w64-mingw32-g++ "${common[@]}" -municode -DFLOGGFY_PUBLICATION_TEST -I native/vendor/libogg/include \
 tests/windows_publication_test.cpp native/recording_file.cpp native/vorbis_headers.cpp build/windows-tests/vorbis-headers-stb.o native/existing_quality.cpp native/file_publication.cpp native/metadata_update_core.cpp native/library_layout.cpp "${quality_sources[@]}" -o "$floggfy_test_root/windows-publication-test.exe"
"$floggfy_test_root/windows-publication-test.exe"

# Metadata fills work for existing FLAC and Ogg without publishing new audio.
x86_64-w64-mingw32-g++ "${common[@]}" -DFLOGGFY_PUBLICATION_TEST -I native/vendor/libogg/include \
 tests/windows_metadata_update_test.cpp native/metadata_update_windows.cpp native/metadata_update_core.cpp native/recording_file.cpp native/vorbis_headers.cpp build/windows-tests/vorbis-headers-stb.o native/existing_quality.cpp native/file_publication.cpp native/library_layout.cpp "${quality_sources[@]}" native/history_settings.cpp native/async_log.cpp \
 -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-metadata-update-test.exe"
"$floggfy_test_root/windows-metadata-update-test.exe"

# Storage failure returns an error without dereferencing a failed file mapping.
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include \
 tests/windows_recording_read_test.cpp native/recording_file.cpp native/vorbis_headers.cpp build/windows-tests/vorbis-headers-stb.o native/library_layout.cpp "${quality_sources[@]}" native/history_settings.cpp native/async_log.cpp \
 -lole32 -luuid -lshell32 -o "$floggfy_test_root/windows-recording-read-test.exe"
"$floggfy_test_root/windows-recording-read-test.exe"

# Focused correction regressions also remain part of the normal Windows suite.
for issue in 1 2 3 5; do
    bash test-pr11-corrections.sh "$issue"
done
