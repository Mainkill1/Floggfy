# Floggfy

A music downloader mod for the Windows Spotify client, inspired by [Soggfy](https://github.com/Rafiuth/Soggfy).

## Features

- Saves fully played tracks in their original Ogg or FLAC format.
- Embeds cover art and metadata, including lyrics when already cached by the client.
- Organizes music by artist and album; skips existing files unless a quality upgrade is available.
- Saves in the background, with an optional activity log capped at 5 MiB.

## Installation and usage

Tested with **Windows x64 Spotify 1.3.1.234**. Other versions and Microsoft Store installs are unvalidated.

1. Quit Spotify and download the ZIP from [Releases](https://github.com/Mainkill1/Floggfy/releases).
2. Open your Spotify installation folder (usually `%APPDATA%\Spotify`)
3. Copy `version.dll` there, then copy `SpotifyHistory.ini.example` as `SpotifyHistory.ini`.
4. Start Spotify, open the top-left menu → **To Disk**, and enable **Downloads**. Play a track from start to finish without seeking or skipping.

<img width="670" height="172" alt="image" src="https://github.com/user-attachments/assets/e0f5f092-03db-4a37-9d63-02ef7d7051f0" />


Tracks save to your Windows Music folder under `Spotify/Artists/Artist/Album`.
To uninstall, quit Spotify, remove Floggfy's `version.dll` and restore your backup.

## Settings

**To Disk** provides Downloads, Save Location, FLAC and Ogg controls.
See the [example INI](SpotifyHistory.ini.example) and [configuration guide](AUDIO-HISTORY.md) for other settings.

<img width="357" height="303" alt="image" src="https://github.com/user-attachments/assets/5e0be853-c888-40b4-a8af-27f140a28243" />


If Spotify becomes unstable, quit it and try `Metadata=0`, then `Menu=0` in the INI.
Restart after editing. [More troubleshooting](AUDIO-HISTORY.md#troubleshooting).

## Notes

- Audio quality comes from Spotify's playback settings. FLAC requires a lossless source; no conversion or FFmpeg is used.
- Extra metadata uses existing client caches only. No endpoint requests are made, and missing fields are omitted.
- Spotify updates can disable capture until support is updated. See [validation and limitations](VALIDATION.md).

## Credits

[Rafiuth/Soggfy](https://github.com/Rafiuth/Soggfy), [Soggfy-Fixed](https://github.com/SuperSecretEyeball/Soggfy-Fixed) and [Spicetify](https://github.com/spicetify/cli).

[MIT license](LICENSE). Third-party licenses are included. Not affiliated with Spotify.
