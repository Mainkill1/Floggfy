# Never Pause

Add the requested Never Pause check item to To Disk. `NeverPause=-1` is the default and hides/disables it; `0` shows it off; `1` shows it on. Invalid, missing or duplicate values fall back to -1. Menu changes persist and take effect on the next poll without resetting capture.

While enabled, detect paused local Spotify playback and issue Resume/Play, including intentional pauses and terminal/buffering states. Reuse the existing 500 ms renderer heartbeat and discovered PlayerAPI; prefer resume, then play, then the existing wrapper play capability. Never use a toggle command. Retry at most once per two seconds and allow only one unsettled command. Disabled, hidden, playing, remote and unknown-local states send no command. Missing capabilities and rejected commands leave polling alive. This is an opt-in playback control, not a diagnosis of the reported pause issue.

## Approach

Use the existing renderer loop/settings transport: it already owns Spotify state and targeted player commands. A separate Windows media-session worker would add session discovery and async lifetime management; global media keys could control another app. The existing-loop choice is an implementation decision; the user specified the name, paused-to-resume behavior and INI states.
