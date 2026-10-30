# Floggfy

Save the music you play in Spotify on Windows.

- Saves fully played songs with available artwork, metadata and lyrics.
- Organizes music by album artist and album.
- Skips existing songs unless better audio is available, and can fill in missing metadata.

## Install

- Requires the **Windows x64 Spotify desktop app**.
- Quit Spotify completely before installing or upgrading.
- Download your preferred option from [Releases](https://github.com/Mainkill1/Floggfy/releases):
- **Always on:** put `version.dll` beside `Spotify.exe`, then start Spotify normally.
- **Launcher:** put `Floggfy.exe` and `Floggfy.dll` beside `Spotify.exe`, then run `Floggfy.exe`.
- The Spotify folder is usually `%APPDATA%\Spotify`.
- Use **one installation option**. Remove Floggfy's `version.dll` when switching to the launcher.
- The launcher closes and restarts Spotify. To run without Floggfy, quit Spotify and start it normally.

<img width="670" height="172" alt="Files beside Spotify.exe" src="https://github.com/user-attachments/assets/4efec58b-663b-4d3d-af31-4bf9fa04b0e1" />

## Save music

- Downloads are enabled by default. Open Spotify's top-left menu → **To Disk** to change settings.
- Play songs from beginning to end without skipping or seeking.
- **Disable Automix** under **Edit → Preferences → Playback**. It trims songs and prevents complete saves.
- Files save to `Music\Spotify\Artists\Artist\Album` by default. Change this with **Save Location**.
- For FLAC, select lossless playback in Spotify.
- Click **Song** to open a saved song's folder. Unsaved songs stay greyed out.
- **Latest Downloads** lists the last ten songs saved during the current run.

<img width="338" height="362" alt="To Disk menu" src="https://github.com/user-attachments/assets/b6294a3d-c09d-4dba-926a-72a1529d1db7" />

## Settings

- **To Disk** provides **Downloads**, **Save Location**, **FLAC**, **Ogg** and **Lyrics** controls.
- `SpotifyHistory.ini` is created automatically. Keep it when upgrading, and fully restart Spotify after editing it.
- `UpdateExistingMetadata=1` fills missing details, lyrics and better artwork on verified existing songs as enrichment arrives, including while paused, without finishing the song again. Files held open by another app produce a warning and retry while the track remains current. Set `0` to leave their metadata unchanged.
- `TrackNumberInFilename=0` uses title-only filenames. Set `1` to add track numbers.
- `ConvertNumberedFilenames=0`: set `1` to remove old track-number prefixes. Numbering disables conversion.
- `AAC=1` enables AAC saving as M4A. It defaults to `0`.
- `Menu=0` disables the menu; configured saving continues.
- Playback tracking uses Spotify app state and track URIs, with Windows media sessions as a fallback.
- `Metadata=0` disables enrichment while retaining app playback tracking.
- `Log=1` writes normal activity, warnings and failures to `Floggfy.log`. `DebugLog=1` writes optional detail only to `Floggfy-diagnostics.log`. Each file is capped at 5 MiB.
- `NetworkPatch=1` enables a workaround for Windows incorrectly reporting no Internet. It defaults to `0`.
- See [settings and troubleshooting](AUDIO-HISTORY.md) for all options, including advanced diagnostics.

## Having trouble?

- **Doesn't load:** check that Spotify is x64 and try the launcher option.
- **Songs aren't saved:** check **Downloads**, play a complete song and disable Automix.
- **Playback problems:** try `Menu=0`, then `Metadata=0`, restarting after each change.
- **Need help:** [report an issue](https://github.com/Mainkill1/Floggfy/issues) with your Spotify version and relevant log entries.
- Spotify updates may require a newer Floggfy release.
- To uninstall, quit Spotify and remove the Floggfy files you installed.

## Support development

[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Development-FF5E5B?logo=kofi&logoColor=white)](https://ko-fi.com/mainkill1)

## Legal notice

This software records music already being played. It does not decrypt or independently download music from Spotify, and does not promote piracy or music sharing.

Output is intended for personal use and should not be shared.

## Credits

- Based on [Soggfy](https://github.com/Rafiuth/Soggfy).
- Additional references: [Soggfy-Fixed](https://github.com/SuperSecretEyeball/Soggfy-Fixed) and [Spicetify](https://github.com/spicetify/cli).
- [MIT license](LICENSE). Not affiliated with Spotify.
