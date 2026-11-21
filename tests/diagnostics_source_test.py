"""Structural regressions for work that must never enter the capture queue."""
import json
import subprocess
import sys
from pathlib import Path

audio = Path('native/media/audio_history.cpp').read_text()
for retired in ('FormatHook', 'FormatDetected', 'original_sniff', 'status calls=', 'prefix=%s'):
    assert retired not in audio, f'diagnostic-only capture work returned: {retired}'
discovery = Path('native/tooling/spotify_hook_discovery.cpp').read_text()
assert 'kFormatSniff' in discovery and 'TargetKind::FormatSniff' in discovery
settings = Path('native/tooling/history_settings.cpp').read_text()
assert 'void HistoryLog(' not in settings and 'std::strstr(message,' not in settings
subprocess.run([sys.executable, 'tools/embed_assets.py'], check=True)
header = Path('build/default_ini.h').read_text()
text = json.loads(header.split('[]=', 1)[1].removesuffix(';\n'))
assert text.replace('\r\n', '\n') == Path('SpotifyHistory.ini').read_text(), 'embedded/public defaults differ'
assert text.index('DebugHookDiagnostics=0') < text.index('DumpStreams=0'), 'DumpStreams must remain last in the diagnostics options'
assert '; Saves raw streams and matching metadata to Save Location\\DumpStreams; restart required.\r\nDumpStreams=0' in text, 'DumpStreams needs a concise destination description'
print('PASS: diagnostic format detour absent, FormatSniff discovery retained, no text classifier, exact embedded defaults')
