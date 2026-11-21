# Shared by Windows fixture runners after they change to the repository root.
windows_test_dir() {
    local windows_temp temp_path
    windows_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r') || return
    temp_path=$(wslpath -u "$windows_temp") || return
    mkdir -p "$temp_path/$1" && printf '%s\n' "$temp_path/$1"
}

build_windows_minhook() {
    local object_dir=$1 source
    for source in buffer hook trampoline hde/hde64; do
        x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
            "native/vendor/minhook/src/$source.c" -o "$object_dir/minhook-${source//\//-}.o"
    done
}

build_windows_audio_objects() {
    local object_dir=$1
    shift
    x86_64-w64-mingw32-gcc -std=c11 -O2 "$@" -c native/vendor/minimp4/minimp4.c -o "$object_dir/minimp4.o"
    x86_64-w64-mingw32-gcc -std=c11 -O2 "$@" -c native/media/vorbis_headers_stb.c -o "$object_dir/vorbis.o"
    x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o "$object_dir/ogg.o"
}

windows_cxxflags=(-std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++)

quality_sources=(
    native/media/playback_quality.cpp native/media/flac_history_core.cpp native/media/compressed_buffer.cpp
    native/media/image_info.cpp native/media/ogg_tags.cpp native/media/ogg_history_core.cpp
)
recording_sources=(
    native/media/recording_file.cpp native/media/aac_container.cpp native/media/spac_stream.cpp
    native/media/mp3_stream.cpp native/media/vorbis_headers.cpp
)
# The directory lookup fixture supplies its own existing_quality.cpp and song_destination.cpp.
capture_sources=(
    "${recording_sources[@]}" "${quality_sources[@]}" native/media/playback_quality_windows.cpp
    native/media/media_session.cpp native/media/library_layout.cpp native/media/file_publication.cpp
    native/media/metadata_update_core.cpp native/media/metadata_update_windows.cpp native/ui/recent_downloads.cpp
    native/tooling/spotify_hook_discovery.cpp native/media/playback_identity_bridge.cpp native/media/auto_skip.cpp
    native/media/decoder_ownership.cpp native/tooling/async_log.cpp
)
