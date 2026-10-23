# Floggfy

## Support Development

If you find this project useful, consider supporting its development.

[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Development-FF5E5B?logo=kofi&logoColor=white)](https://ko-fi.com/mainkill1)


Save the music you play in Spotify on Windows.

- Saves fully played tracks as Ogg or FLAC, with cover art and available song details.
- Sorts music by artist and album.
- Skips existing audio unless a better-quality version is available; existing FLAC files can also receive lyric upgrades.

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
The launcher will automatically close and restart Spotify when ran. To return to normal Spotify, quit Spotify completely then launch it like normal.

## Save music

Downloads starts enabled on a new installation. Use Spotify's top-left menu → **To Disk** to pause or resume saving. Existing saved preferences are preserved.
Play a song from start to finish without skipping or seeking.

Music saves under `Spotify/Artists/Artist/Album` in your Windows Music folder.

FLAC needs lossless playback in Spotify. Floggfy does not convert Ogg into FLAC.

## Settings

**To Disk** includes **Downloads**, **Save Location**, **FLAC**, **Ogg** and **Lyrics** controls.
After a successful download, **Latest Downloads** appears directly below **Save Location**.
It lists up to ten recently saved songs from this Spotify run; click one to open
Explorer at its saved folder.
Song remains visible, and Quality appears when known. Format and Sample rate
appear only for confirmed FLAC playback; unavailable technical rows are hidden.

The current **Song** row stays visible but greyed out until a matching saved
FLAC or Ogg file exists. Click it to open the containing folder. If the file was removed,
an unavailable notice is shown. Folders are created only when saving audio;
opening a folder does not count as a download.

Lyrics are collected automatically when Spotify provides them; the lyrics panel
does not need to be open. Timed lyrics are embedded as LRC text in the `LYRICS`
tag; lyrics without timing use plain text. No separate `.lrc` file is created.
After a complete replay, an existing FLAC can receive missing lyrics or upgrade
plain lyrics to synchronized lyrics, even when the current playback quality is
lower. Its audio, artwork and other tags stay intact. Already synchronized lyrics
are retained, and audio quality upgrades keep existing lyrics if needed.
Turn **Lyrics** off in **To Disk**, or set `Lyrics=0` in
the INI, if a Spotify update causes compatibility problems. Album artwork and
other available metadata continue to work. `Metadata=0` disables all extra metadata collection.

<img width="338" height="362" alt="image" src="https://github.com/user-attachments/assets/b6294a3d-c09d-4dba-926a-72a1529d1db7" />


`SpotifyHistory.ini` is created automatically. Keep your existing file when
upgrading. Settings are stored as Unicode so save locations survive restart;
older INI files are migrated while preserving their preferences. Use it to disable the menu, extra song details or activity log. At the 5 MiB log limit, the oldest complete
lines are removed as new entries arrive. Completed saves include the audio quality
and final file size. Skipped songs are logged with the reason, including an
existing file of the same or better quality.

## Having trouble?

- **Floggfy doesn't load:** try the launcher option.
- **Spotify crashes:** quit it, set `Metadata=0` in `SpotifyHistory.ini`, and restart. If needed, try `Menu=0` too.
- **Menu missing:** quit Spotify and set `Downloads=1` in the INI to save without it.

Tested with Windows x64 Spotify **1.2.93.667** and **1.3.6.1921**. Updates may need a new Floggfy release. [Report a problem](https://github.com/Mainkill1/Floggfy/issues).

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
