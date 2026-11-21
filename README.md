# Floggfy

Save the music you play in Spotify on Windows.

Development checks covered Spotify 1.2.97, 1.3.9, and 1.3.10 on Windows x64. The final cross-version compatibility checks used telemetry blocking disabled.

- Saves fully played songs with available artwork, metadata and lyrics.
- Organizes music by album artist and album.
- Skips existing songs unless better audio is available, and can fill in missing metadata.

## Install

- Requires the **Windows x64 Spotify desktop app**.
- Quit Spotify completely before installing or upgrading.
- Download your preferred option from [Releases](https://github.com/Mainkill1/Floggfy/releases):
- **Always on:** put `version.dll` beside `Spotify.exe`, then start Spotify normally.
- **Launcher:** put `Floggfy.exe` and `Floggfy.dll` beside `Spotify.exe`, then run `Floggfy.exe`. Launcher mode loads the same DLL into spawned child processes before they start.
- The Spotify folder is usually `%APPDATA%\Spotify`.
- Use **one installation option**. Remove Floggfy's `version.dll` when switching to the launcher.
- The launcher closes and restarts Spotify. To run without Floggfy, quit Spotify and start it normally.

<img width="670" height="172" alt="Files beside Spotify.exe" src="https://github.com/user-attachments/assets/4efec58b-663b-4d3d-af31-4bf9fa04b0e1" />

## Save music

- Downloads are enabled by default. Open Spotify's top-left menu → **To Disk** to change settings.
- Play songs from beginning to end without skipping or seeking.
- **Capture Speed** defaults to **1x**. Faster settings are intended for muted,
  unattended capture: the saved compressed audio is unchanged, but live playback
  can sound choppy and Spotify's progress display may be inaccurate. The Capture
  Speed menu is hidden by default. Set `CaptureSpeed=1` in
  `SpotifyHistory.ini` and fully restart Spotify to enable it.
- **Disable Automix** under **Edit → Preferences → Playback**. It trims songs and prevents complete saves.
- Music saves under `Music\Spotify\Artists\Artist\Album`; enabled podcasts save under `Podcasts\Show`.
- New filename collisions go under `Duplicates`, retaining the artist/album hierarchy. Files without a valid Spotify track ID use `Title [no-id-<hash>]`; known IDs use `Title [t-<id>]`. Existing recordings are reused in place.
- For FLAC, select lossless playback in Spotify.
- Click **Song** to open a saved song's folder. Unsaved songs stay greyed out.
- **Latest Downloads** lists the last ten songs saved during the current run.

<img width="379" height="442" alt="image" src="https://github.com/user-attachments/assets/cd8bcd48-35fb-4991-8cf3-8614f076ad44" />


## Settings

- **To Disk** keeps Downloads, Latest Downloads, song details and optional Capture Speed/Never Pause controls together. **Allowed Formats** contains Ogg, FLAC, AAC, Podcasts and Include Lyrics (last); **Misc** contains Save Location, Auto Skip Downloaded, Marked When Downloaded, Marked DB Rebuild, Block Updates and the optional Network Kill Switch; **Fallbacks** contains enrichment controls and Windows Playback Fallback.
- **Capture Speed:** `CaptureSpeed=-1` (default) hides the menu and uses 1x. `0` or `1` shows it at 1x; `2`, `4`, `8` and `10` select faster recording when acceleration is available.
- **Never Pause** resumes paused local Spotify playback, including manual pauses.
  `NeverPause=-1` (default) hides and disables it; `0` shows it off; `1` shows it on.
  Menu changes apply immediately. Restart Spotify after changing the INI.
- `SpotifyHistory.ini` is created automatically. Keep it when upgrading, and fully restart Spotify after editing it.
- `[Misc]` contains `UpdateExistingMetadata`, `TrackNumberInFilename`, and `ConvertNumberedFilenames`; older `[To Disk]` values migrate automatically.
- `UpdateExistingMetadata=1` fills missing details, lyrics, and improved artwork as soon as playback identifies an owned existing song; finishing another listen is not required. Set `0` to leave its metadata unchanged.
- `TrackNumberInFilename=0` uses title-only filenames. Set `1` to add track numbers.
- `ConvertNumberedFilenames=0`: set `1` to remove old track-number prefixes. Numbering disables conversion.
- `AAC=1` enables AAC saving as M4A. It defaults to `0`.
- **Podcasts** is enabled by default. Complete episodes retain their native MP3,
  Ogg, FLAC, or enabled AAC audio under `Podcasts\Show`.
- `Menu=0` disables the menu; configured saving continues.
- Playback tracking uses Spotify app state and track URIs. `WindowsPlaybackFallback=1` under `[Fallbacks]` enables the Windows media-session compatibility fallback; it defaults to `0`.
- `Metadata=0` disables enrichment while retaining app playback tracking. Available song and podcast tags stay attached to the recording; online enrichment fills gaps only after local collection.
- Artwork first checks Spotify’s native image service using the track’s exact image references, then falls back to the browser cache and an online request. Unsupported clients retain the fallback paths. Bundled WebP decoding and PNG writing require no installed image codecs or extra DLLs.
- **To Disk > Fallbacks** controls Spotify disk metadata reads and extra requests for missing track and album tags, artwork, and lyrics. All four are on by default and can be changed live. Available data stays usable. **Include Lyrics** controls whether lyrics are saved.
- `MetadataCacheReadMiB=256` under `[History]` limits disk reads per cached metadata lookup (1–4096 MiB). Local reads run in the background without a fixed time cutoff.
- `[Matcher]` defaults to `TimeOffsetToleranceMs=10000` (0–120000 ms) and `DurationToleranceMs=25` (0–2000 ms). Exact playback IDs can match outside the timing window.
- `MaxMatchQueue=5` under `[Matcher]` sets the subsequent media starts before unmatched retirement (1–100).
- `SaveUnmatched=1` under `[Matcher]` saves complete, codec-validated unidentified streams in `Unmatched` at retirement. Incomplete or corrupt preloads are discarded. Set `0` to discard unmatched streams too.
- `DumpStreams=1` under `[Debug]` saves raw streams and matching metadata for debugging; it is separate from complete recordings in `Unmatched`.
- `[Debug]` defaults to `LogLevel=1` and `DebugConnect=0`. Values `0/1/2/4` select off, finished outcomes, visible lifecycle, or full diagnostics. Debug, networking, and hook evidence use separate capped files; see the advanced settings guide.
- `[Telemetry]` has a master `BlockTelemetry` switch (default `0`) and separate category controls for event, playback, growth, Sentry, crash, internal, track-error and AI reports. `BlockQueuedTelemetry=1` clears known pending tracking files at startup and normal exit and filters their known destinations when the master is on. `DumpTelemetry=1` logs blocked requests. Coverage is partial; see [privacy settings and testing](docs/telemetry-testing.md).
- `[Misc]` contains `NetworkPatch=0`, an optional workaround for Windows incorrectly reporting no Internet, and `NetworkKillSwitch=0` (off by default): `-1` hides the switch, `0` shows it off, `1` shows it on.
- **Marked When Downloaded** (`MarkedWhenDownloaded=1` under `[Misc]`) adds a Saved date to supported playlist layouts. It uses the latest recording's `HISTORY_SAVED_UTC` tag and a local `Floggfy-marked.db` in Save Location; unknown tracks stay blank. Click Saved for newest first, oldest first, then original playlist order. Sorting covers the whole playlist, with unmarked songs last. With shuffle off, playlist playback follows the Saved order. Turn it off to hide the column and stop indexing.
- **Marked DB Rebuild** starts or cancels a background scan of existing music. New recordings are indexed automatically. The scan imports tagged dates; when Downloads, Metadata and UpdateExistingMetadata are on, missing dates are filled from file creation time. Metadata updates preserve existing saved dates. DumpStreams and Unmatched are excluded. External moves into the library require another rebuild; visible indexed files are rechecked about every 30 seconds.
- **Block Updates** under Misc (`BlockUpdates=0` by default) restricts the local Spotify Update folder. Enabled startup checks the restriction; disabling restores parent-folder permissions. Disable it before uninstalling Floggfy to restore updates.
- **Network Kill Switch** blocks covered IPv4/IPv6 Winsock operations in processes loading Floggfy. `HookAllSubProcesses=1` (default) carries launcher coverage into spawned network helpers; `0` disables child loading and `2` enables it for every load method, including `version.dll`. It cannot cancel already queued I/O, direct native I/O or cached RIO paths; it is not a firewall guarantee. See [settings and coverage](AUDIO-HISTORY.md#settings).
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
