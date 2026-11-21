#!/usr/bin/env bash
# One Windows fixture for restart-only network opt-in and settings persistence.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
floggfy_network_root=$(windows_test_dir "Floggfy-network-patch-$(date +%s)-$$")
python3 tools/embed_assets.py
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" -municode \
    tests/windows_network_patch_test.cpp tests/marked_catalog_stub.cpp native/tooling/pe_imports.cpp \
    -lole32 -luuid -lshell32 -liphlpapi -lws2_32 -o "$floggfy_network_root/network-patch-test.exe"
exec "$floggfy_network_root/network-patch-test.exe"
