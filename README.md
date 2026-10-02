# Floggfy

A music downloader mod for the Windows Spotify client, inspired by [Soggfy](https://github.com/Rafiuth/Soggfy).

## Features

- Saves fully played tracks in their original Ogg or FLAC format.
- Embeds cover art and metadata, including lyrics when already cached by the client.
- Organizes music by artist and album; skips existing files unless a quality upgrade is available.
- Saves in the background, with an optional activity log capped at 5 MiB.
- Shows the current song, quality level, codec and sample rate at the bottom of **To Disk**, even with Downloads off.

## Installation and usage

Live tested with **Windows x64 Spotify 1.3.3.264**. Dynamic audio and connectivity
discovery was also checked against signed Spotify DLLs from 1.3.0.277,
1.2.94.583 and 1.2.92.148; those older clients were not run end to end.
Microsoft Store installs are unvalidated.

1. Quit Spotify and download the ZIP from [Releases](https://github.com/Mainkill1/Floggfy/releases).
2. Open your Spotify installation folder, usually `%APPDATA%\Spotify`.
3. Copy `version.dll` into that folder beside `Spotify.exe`.
4. Start Spotify. Floggfy creates `SpotifyHistory.ini` beside the DLL when it is missing.
5. Open Spotify's top-left menu → **To Disk**, enable **Downloads**, and play a track from start to finish without seeking or skipping.

If Spotify skips the local DLL on normal startup, quit Spotify,
copy the included `Floggfy.exe` beside `Spotify.exe` and `version.dll`, then
start `Floggfy.exe`. This optional launcher explicitly loads the adjacent DLL
before application startup. Keep the EXE and DLL from the same release.

Tracks save to your Windows Music folder under `Spotify/Artists/Artist/Album`.
To uninstall, quit Spotify and remove Floggfy's `version.dll` and optional `Floggfy.exe`.

## Settings

**To Disk** provides Downloads, Save Location, FLAC and Ogg controls.
The bottom rows refresh when the submenu opens. Quality uses the current cached
player state or validated decoder headers. Track information refreshes once per second.
Unavailable values mean the source has not yet been matched or fully measured.
See the inline comments in [SpotifyHistory.ini](SpotifyHistory.ini)
for other settings.

If Spotify becomes unstable, quit it and try `Metadata=0`, then `Menu=0` in the INI.
Restart after editing.

If **To Disk** is absent after a Spotify update, quit Spotify and set
`Downloads=1` directly in `SpotifyHistory.ini`. Audio capture does not depend on
the optional menu or metadata integrations.

## Notes

- Audio quality comes from Spotify's playback settings. FLAC requires a lossless source; no conversion or FFmpeg is used.
- Extra metadata reads existing renderer and local Spotify caches in the background. It includes contributing artists, release dates, labels and copyright when cached; no endpoint requests are made. Explicit publisher credits stay separate from labels, and missing fields are omitted.
- Audio hook locations are discovered from invariant decoder instructions and
  Windows x64 function metadata. Missing, ambiguous or inconsistent matches disable
  capture before any hook is installed.
- Connectivity repair finds `CoCreateInstance` by its PE import name across normal
  and delay import tables, independent of Spotify hashes, offsets and the provider
  DLL name. It keeps verifying the live delay slot because Windows may replace it
  during delayed resolution.
- The **To Disk** menu finds CEF through its exported factory, validates the live
  public menu structures and required executable methods, and recognizes the
  top-level menu structurally. It has no Spotify offsets, CEF version allowlist or
  English `File`/`Edit`/`View` dependency.

## Credits

[Rafiuth/Soggfy](https://github.com/Rafiuth/Soggfy), [Soggfy-Fixed](https://github.com/SuperSecretEyeball/Soggfy-Fixed) and [Spicetify](https://github.com/spicetify/cli).

[MIT license](LICENSE). Third-party licenses are included. Not affiliated with Spotify.
