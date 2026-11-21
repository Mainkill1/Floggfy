#!/usr/bin/env bash
# Isolated policy/producer/CEF callback tests. No Spotify execution or network requests.
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build/telemetry-tests
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/telemetry_policy_test.cpp -o build/telemetry-tests/policy
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/telemetry_decode_test.cpp native/telemetry_decode.cpp native/telemetry_inflate.cpp -o build/telemetry-tests/decode
build/telemetry-tests/decode
build/telemetry-tests/policy
node tests/telemetry_producers_test.js
floggfy_privacy_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_privacy_root="$(wslpath -u "$floggfy_privacy_temp")/Floggfy-telemetry-$(date +%s)-$$"
mkdir -p "$floggfy_privacy_root"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
    tests/windows_telemetry_queue_test.cpp -lole32 -luuid -lshell32 -o "$floggfy_privacy_root/queue.exe"
"$floggfy_privacy_root/queue.exe"
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/telemetry-tests/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode -static-libgcc -static-libstdc++ \
    tests/windows_telemetry_queue_exit_test.cpp native/telemetry_queue_exit.cpp native/telemetry_queue.cpp \
    build/telemetry-tests/minhook-*.o -lole32 -luuid -lshell32 -o "$floggfy_privacy_root/queue-exit.exe"
"$floggfy_privacy_root/queue-exit.exe"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
    tests/windows_telemetry_filter_test.cpp native/telemetry_decode.cpp native/telemetry_inflate.cpp build/telemetry-tests/minhook-*.o -o "$floggfy_privacy_root/filter.exe"
"$floggfy_privacy_root/filter.exe" --dump
"$floggfy_privacy_root/filter.exe" --renderer-dump
"$floggfy_privacy_root/filter.exe" --dump-write-failure
"$floggfy_privacy_root/filter.exe" --disabled
"$floggfy_privacy_root/filter.exe" --missing-exports
"$floggfy_privacy_root/filter.exe" --rollback-before-apply
"$floggfy_privacy_root/filter.exe" --rollback-after-apply
"$floggfy_privacy_root/filter.exe"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
    tests/windows_settings_test.cpp -lole32 -luuid -lshell32 -o "$floggfy_privacy_root/settings.exe"
for scenario in new blocking-legacy-one blocking-legacy-zero blocking-precedence blocking-category-zero; do
    "$floggfy_privacy_root/settings.exe" "$scenario"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
    -municode tests/windows_pr13_settings_test.cpp -lole32 -luuid -lshell32 -o "$floggfy_privacy_root/restart-settings.exe"
"$floggfy_privacy_root/restart-settings.exe"
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/telemetry_crash_command_test.cpp -o build/telemetry-tests/crash-command
build/telemetry-tests/crash-command
mkdir -p "$floggfy_privacy_root/crash"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode -static-libgcc -static-libstdc++ \
    tests/windows_telemetry_crash_test.cpp build/telemetry-tests/minhook-*.o -o "$floggfy_privacy_root/crash/Spotify.exe"
for mode in --disabled --rollback-before-apply --rollback-after-apply; do
    "$floggfy_privacy_root/crash/Spotify.exe" "$mode"
done
"$floggfy_privacy_root/crash/Spotify.exe"
python3 tests/diagnostics_source_test.py
