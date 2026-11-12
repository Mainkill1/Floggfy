# Unresolved duplicate routing

The user approved routing unresolved filename collisions into `Duplicates` and
marking recordings without usable Spotify track IDs as `[no-id-<hash>]`.
Base the change on v1.3-RC6, rather than the older main branch.

Keep canonical matches, legacy aliases, recovery and quality upgrades intact.
If canonical lookup cannot match the recording and would allocate a collision
suffix, use the duplicate namespace even without two valid Spotify IDs. Valid
incoming track IDs retain RC6's compact `[t-<id>]` names. Missing/malformed IDs
use a deterministic 16-digit hash of catalog text and track/disc/duration with
an explicit `no-id` marker. Both remain filename aids, never replacement authority.

Apply the same discovery policy to publication, Song lookup and track-start
metadata/conversion checks. Preserve directory bounds, unreadable-file deferral,
full recording identity checks and atomic publication. Preserve existing files;
do not scan the library or migrate old hex-suffixed recordings.

If a later observation supplies a valid ID, also search the deterministic no-ID
name as a lookup-only alias. Never create a new no-ID file for a valid-ID request.
Keep an existing no-ID suffix during optional track-number conversion. An old
nonempty malformed URI remains subject to the existing conservative URI-match
policy; differing nonempty URIs do not authorize a merge.

Approaches considered: defer unresolved collisions (loses completed captures),
retain RC6's in-album hash fallback (retains the reported problem), or route them
to Duplicates (selected by the user). Only unresolved duplicate destinations get
the no-id marker; ordinary noncolliding filenames retain their current format.
