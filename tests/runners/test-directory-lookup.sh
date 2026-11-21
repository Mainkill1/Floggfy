#!/usr/bin/env bash
# One focused Windows executable for directory-first lookup, conversion and menu.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
mkdir -p build/directory-lookup
floggfy_names_root=$(windows_test_dir "Floggfy-directory-lookup-$(date +%s)-$$")
build_windows_minhook build/directory-lookup
build_windows_audio_objects build/directory-lookup -Wall -Wextra
python3 tools/embed_assets.py
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" -municode \
    -I native/vendor/libogg/include tests/windows_directory_lookup_test.cpp tests/marked_catalog_stub.cpp \
    "${capture_sources[@]}" \
    build/directory-lookup/*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_names_root/directory-lookup-test.exe"
exec "$floggfy_names_root/directory-lookup-test.exe" "$@"
