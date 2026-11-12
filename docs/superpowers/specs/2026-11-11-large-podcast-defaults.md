# Saved INI defaults and large podcasts

Use the user's saved SpotifyHistory.ini as the exact wording, ordering and
default-value template. The final requested MaxBufferedMiB default is 512;
the maximum configurable capture allocation is 1024 MiB. The supplied template
enables Podcasts and retains the other existing switches.

Keep existing explicit settings, including Podcasts=0. Preserve the source INI;
update the bundled file and DLL-generated template. Both must match.

Raise MP3 and shared recording read/publication bounds to 1 GiB so changing the
buffer setting can support large podcasts through saving, lookup and rename.
Increase the MP3 frame-count guard to the supported maximum frame rate over the
existing 24-hour duration limit. Retain corruption, allocation, identity and
no-overwrite checks. AAC-specific construction budgets remain separate.

Verify new/missing defaults, explicit opt-outs, 1024/over/under buffer values,
large parser boundaries and an actual Windows file above the old 512 MiB cutoff.
Large-file fixtures need not be included in every normal suite invocation.
