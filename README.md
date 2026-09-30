# Floggfy

A music downloader mod for the Windows Spotify client, inspired by [Soggfy](https://github.com/Rafiuth/Soggfy).

## Features

- Saves fully played tracks in their original Ogg or FLAC format.
- Embeds cover art and metadata, including lyrics when already cached by the client.
- Organizes music by artist and album; skips existing files unless a quality upgrade is available.
- Saves in the background, with an optional activity log capped at 5 MiB.

## Installation and usage

Live tested with **Windows x64 Spotify 1.3.1.234**. Dynamic audio-hook discovery
was statically validated in signed Spotify DLLs from 1.3.0.277, 1.2.94.583 and
1.2.92.148; those older clients were not run end to end. Microsoft Store installs
are unvalidated.

1. Quit Spotify and download the ZIP from [Releases](https://github.com/Mainkill1/Floggfy/releases).
2. Open your Spotify installation folder (usually `%APPDATA%\Spotify`) and back up any existing `version.dll`.
3. Copy `version.dll` there, then copy `SpotifyHistory.ini.example` as `SpotifyHistory.ini`.
4. Start Spotify, open the top-left menu → **To Disk**, and enable **Downloads**. Play a track from start to finish without seeking or skipping.

Tracks save to your Windows Music folder under `Spotify/Artists/Artist/Album`.
To uninstall, quit Spotify, remove Floggfy's `version.dll` and restore your backup.

## Settings

**To Disk** provides Downloads, Save Location, FLAC and Ogg controls.
See the inline comments in [SpotifyHistory.ini.example](SpotifyHistory.ini.example)
for other settings.

If Spotify becomes unstable, quit it and try `Metadata=0`, then `Menu=0` in the INI.
Restart after editing.

## Notes

- Audio quality comes from Spotify's playback settings. FLAC requires a lossless source; no conversion or FFmpeg is used.
- Extra metadata uses existing client caches only. No endpoint requests are made, and missing fields are omitted.
- Audio hook locations are discovered from invariant decoder instructions and
  Windows x64 function metadata. Missing, ambiguous or inconsistent matches disable
  capture before any hook is installed.

## Credits

[Rafiuth/Soggfy](https://github.com/Rafiuth/Soggfy), [Soggfy-Fixed](https://github.com/SuperSecretEyeball/Soggfy-Fixed) and [Spicetify](https://github.com/spicetify/cli).

[MIT license](LICENSE). Third-party licenses are included. Not affiliated with Spotify.
