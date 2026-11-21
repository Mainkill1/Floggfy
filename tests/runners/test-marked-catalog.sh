#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
windows_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
marked_test_dir=$(mktemp -d "$(wslpath -u "$windows_temp")/Floggfy-marked-catalog.XXXXXXXX")
trap 'rm -rf -- "$marked_test_dir"' EXIT
x86_64-w64-mingw32-gcc -std=c11 -O2 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0 -c native/vendor/sqlite/sqlite3.c -o "$marked_test_dir/sqlite3.o"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
 tests/windows_marked_catalog_test.cpp native/marked_catalog.cpp "$marked_test_dir/sqlite3.o" -o "$marked_test_dir/catalog-test.exe"
"$marked_test_dir/catalog-test.exe"
