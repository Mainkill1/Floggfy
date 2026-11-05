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
- **Capture Speed** defaults to **1x**. Faster settings are intended for muted,
  unattended capture: the saved compressed audio is unchanged, but live playback
  can sound choppy and Spotify's progress display may be inaccurate. They are a
  hidden, default-off feature: set `CaptureSpeedLimit=2` through `10` in
  `SpotifyHistory.ini`, then fully restart Spotify. Values above `10` are limited
  to `10`; missing values and values of `1` or less keep faster menu choices disabled.
- **Disable Automix** under **Edit → Preferences → Playback**. It trims songs and prevents complete saves.
- Music saves under `Music\Spotify\Artists\Artist\Album`; enabled podcasts save under `Podcasts\Show`.
- For FLAC, select lossless playback in Spotify.
- Click **Song** to open a saved song's folder. Unsaved songs stay greyed out.
- **Latest Downloads** lists the last ten songs saved during the current run.

<img width="338" height="362" alt="To Disk menu" src="https://github.com/user-attachments/assets/b6294a3d-c09d-4dba-926a-72a1529d1db7" />

## Settings

- **To Disk** provides **Downloads**, **Save Location**, **FLAC**, **Ogg**, **Podcasts**, **Lyrics** and optional **Capture Speed** controls.
- **Capture Speed** appears only when `CaptureSpeedLimit` is set to `2` through
  `10`; missing, `0`, or `1` keeps the experimental submenu hidden.
- `SpotifyHistory.ini` is created automatically. Keep it when upgrading, and fully restart Spotify after editing it.
- `UpdateExistingMetadata=1` fills missing details, lyrics, and improved artwork as soon as playback identifies an owned existing song; finishing another listen is not required. Set `0` to leave its metadata unchanged.
- `TrackNumberInFilename=0` uses title-only filenames. Set `1` to add track numbers.
- `ConvertNumberedFilenames=0`: set `1` to remove old track-number prefixes. Numbering disables conversion.
- `AAC=1` enables AAC saving as M4A. It defaults to `0`.
- **Podcasts** is a default-off live toggle. Complete episodes retain their native MP3 audio under `Podcasts\Show`.
- `Menu=0` disables the menu; configured saving continues.
- Playback tracking uses Spotify app state and track URIs. `Windows Playback Fallback=1` enables the Windows media-session compatibility fallback; it defaults to `0`.
- `Metadata=0` disables enrichment while retaining app playback tracking.
- `LogLevel=0/1/2/4` selects off, finished outcomes, visible lifecycle, or full diagnostics. Debug, networking, and hook evidence use separate capped files; see the advanced settings guide.
- `NetworkPatch=1` enables a workaround for Windows incorrectly reporting no Internet. It defaults to `0`.
- See [settings and troubleshooting](AUDIO-HISTORY.md) for all options, including advanced diagnostics.

## Having trouble?

- **Doesn't load:** check that Spotify is x64 and try the launcher option.
- **Songs aren't saved:** check **Downloads**, play a complete song and disable Automix.
- **Playback problems:** set **Capture Speed** to **1x** first. Then try `Menu=0`,
  followed by `Metadata=0`, restarting after each change.
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
