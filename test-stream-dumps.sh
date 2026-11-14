#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
floggfy_dump_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_dump_root="$(wslpath -u "$floggfy_dump_temp")/Floggfy-stream-dumps-$(date +%s)-$$"
mkdir -p "$floggfy_dump_root"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode -static-libgcc -static-libstdc++ \
    tests/windows_stream_dump_test.cpp -lshell32 -o "$floggfy_dump_root/test.exe"
"$floggfy_dump_root/test.exe" "$(wslpath -w "$floggfy_dump_root")"
python3 - "$floggfy_dump_root" <<'PY'
import json,sys
from pathlib import Path
root=Path(sys.argv[1])/'DumpStreams';logs=list(root.rglob('*.jsonl'));assert logs
quoted=False
for path in logs:
    rows=[json.loads(line) for line in path.read_text().splitlines()]
    if path.name!='session.jsonl':
        offset=0
        for row in rows:
            if row['kind'] in ('input','capture_event'):assert row['offset']==offset;offset+=row['bytes']
            if row['kind']=='metadata' and row['title']=='Second "poem"\n':quoted=True
        assert path.with_suffix('.bin').stat().st_size==offset
assert quoted,'metadata text must round-trip through valid JSON'
print('PASS: stream metadata is valid JSON with exact offsets, complete raw lengths and escaped text round trips')
PY
