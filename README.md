# Floggfy

Save the music you play in Spotify on Windows.

- Saves fully played tracks as Ogg or FLAC, with cover art and available song details.
- Sorts music by artist and album.
- Skips songs you already have, unless a better-quality version is available.

### Disable Automix under Edit -> Preferences -> Playback as it trims songs and will fail downloads.

## Install

Choose one download from [Releases](https://github.com/Mainkill1/Floggfy/releases):

| Option | Files to install | How to start |
| --- | --- | --- |
| **Always on** | `version.dll` | Open Spotify normally. |
| **Launcher** | `Floggfy.exe` and `Floggfy.dll` from the Launcher ZIP | `Floggfy.exe` with Floggfy; Spotify normally without it. |

Open your Spotify folder, usually `%APPDATA%\Spotify`.
Put your chosen files beside `Spotify.exe`.

<img width="670" height="172" alt="image" src="https://github.com/user-attachments/assets/4efec58b-663b-4d3d-af31-4bf9fa04b0e1" />


Use only one option. Remove Floggfy's `version.dll` when switching to the launcher.
The launcher closes and restarts Spotify. Quit Spotify completely before switching
back to plain Spotify.

## Save music

Open Spotify's top-left menu → **To Disk** → enable **Downloads**.
Play a song from start to finish without skipping or seeking.

Music saves under `Spotify/Artists/Artist/Album` in your Windows Music folder.

FLAC needs lossless playback in Spotify. Floggfy does not convert Ogg into FLAC.

## Settings

**To Disk** includes **Downloads**, **Save Location**, **FLAC** and **Ogg** controls.

The bottom shows the current item and available quality details.

<img width="338" height="362" alt="image" src="https://github.com/user-attachments/assets/b6294a3d-c09d-4dba-926a-72a1529d1db7" />


`SpotifyHistory.ini` is created automatically. Keep your existing file when
upgrading. Use it to disable the menu, extra song details or activity log.

## Having trouble?

- **Floggfy doesn't load:** try the launcher option.
- **Spotify crashes:** quit it, set `Metadata=0` in `SpotifyHistory.ini`, and restart. If needed, try `Menu=0` too.
- **Menu missing:** quit Spotify and set `Downloads=1` in the INI to save without it.

Tested with Windows x64 Spotify **1.3.3.264**. Updates may need a new Floggfy release. [Report a problem](https://github.com/Mainkill1/Floggfy/issues).

To uninstall, quit Spotify and remove the Floggfy files you installed.

## Legal Notice
This software does not decrypt or download music from Spotify.
To be exact, it simply "records" the music that is being played.
This software does not promote piracy or music sharing in anyway.

Any output this software provides should not to be shared, and is intended for personal use only. 

## Credits

Based on [Soggfy](https://github.com/Rafiuth/Soggfy), with work reviewed from
[Soggfy-Fixed](https://github.com/SuperSecretEyeball/Soggfy-Fixed) and
[Spicetify](https://github.com/spicetify/cli).

[MIT license](LICENSE). Not affiliated with Spotify.
