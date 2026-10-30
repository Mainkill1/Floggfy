"""Structural regressions for work that must never enter the capture queue."""
import ast
from pathlib import Path
import re

audio = Path('native/audio_history.cpp').read_text()
for retired in ('FormatHook', 'FormatDetected', 'original_sniff', 'status calls=', 'prefix=%s'):
    assert retired not in audio, f'diagnostic-only capture work returned: {retired}'
discovery = Path('native/spotify_hook_discovery.cpp').read_text()
assert 'kFormatSniff' in discovery and 'TargetKind::FormatSniff' in discovery
settings = Path('native/history_settings.cpp').read_text()
assert 'void HistoryLog(' not in settings and 'std::strstr(message,' not in settings
embedded = settings.split('static constexpr char default_ini[]=\n', 1)[1].split('static bool CreateDefaultIni', 1)[0]
text = ''.join(ast.literal_eval(match) for match in re.findall(r'"(?:[^"\\]|\\.)*"', embedded))
assert text.replace('\r\n', '\n') == Path('SpotifyHistory.ini').read_text(), 'embedded/public defaults differ'
print('PASS: diagnostic format detour absent, FormatSniff discovery retained, no text classifier, exact embedded defaults')
