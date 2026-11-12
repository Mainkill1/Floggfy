# Stream-owned recording

The recorder should save a complete, identified audio stream even when Spotify's
playback observations arrive late, out of order, or stop before the final tick.
Playback observations supply identity and metadata; they do not decide whether
compressed audio is complete or whether its memory should be discarded.

## Evidence

The November 12 playlist dump contains Queen's entire 4.033061-second Ogg stream:
23 pages, 73,889 bytes, and a valid end page. The current recorder rejects its
playback association when a natural transition timestamp arrives 16 milliseconds
before the latest end-position observation. A prefetched next stream also defeats
the timing-based fallback. The local session replay reproduces the rejection.

## Approach

The user selected three independent stages: stream collection, metadata/history
collection, and a sole dedicated matcher/finalizer. Keep the existing codec
readers and their validation; simplify the coordination around them.

## Flow

1. Stream collection gives each decoder/logical stream a stable generation ID
   and its own compressed buffer, start/end timestamps, measured length, and
   codec completeness state. Multiple streams can coexist during prefetch.
   A new stream signals a boundary; it cannot manufacture missing data or close
   another decoder that is still supplying its tail.
2. Metadata/history collection independently retains stable playback-instance
   generations, full Media snapshots, start/end observations, and reported
   duration. Early, late, paused, skipped, or reordered UI updates do not delete
   captured bytes. An address, reused Ogg serial, or dump filename is not a
   recording identity.
3. The dedicated matcher/finalizer alone joins these records, decides readiness
   and unmatched retirement, and schedules publication. Complete identified
   audio can publish immediately without a completed listen or end-position
   tick, even before audible playback ends. Publication retains format, tags,
   artwork, lyrics, duplicate routing, and existing-file rules. Successful saves
   or verified existing-file matches release the buffer; retries retain it.

The lifecycle is collecting → complete/awaiting metadata → ready to publish →
queued/published. Queue-full retries keep the same recording and association.
Metadata association uses a stable playback-instance generation independent of
listening coverage. It retains the full Media snapshot, including when paused.
Decoder reuse and Ogg serial reuse create distinct logical stream generations;
a dump file or decoder address is not itself one recording.

## Integrity and association

- Ogg: verify serial, page sequence, CRC, headers, granule continuity, and EOS.
  A new BOS without the previous tail closes an incomplete candidate; it does
  not turn it into a valid file.
- FLAC: verify STREAMINFO and continuous decoded sample/frame coverage through
  the advertised sample count.
- AAC/MP4: use container/packet boundaries and declared sample coverage.
- MP3: use source boundaries, valid frame sequences, declared frame/sample
   counts when present to detect truncated sources. Metadata length is an
   association filter, not proof that a frame-aligned prefix is complete.
  A pause or silence alone is never an MP3 boundary.
  Validate Xing/VBRI declared counts before using them as completion proof.
  Without declared-count proof, a final item with no successor must await a real source
  closure; elapsed time and approximate duration are insufficient.
- Prefer exact native playback ID plus decoder ownership. When that capability
  is unavailable, retain a conservative unique association based on recorded
  stream and metadata evidence. Ambiguous identity waits; cleanup cannot make
  an ambiguous association become certain.
  Exact identity is decisive: another prefetched stream is not an alternative
  identity for that recording. Fallback must match one recording to one retained
  metadata instance using positive decoder-stage evidence, codec duration, and
  compatible capture timing. Check both directions against active and completed
  candidates; same-duration tracks, repeats, ads, and unclassified streams must
  not become unique merely because a competing record was removed. Where the
  available evidence cannot prove that association, retain it as unresolved.
  Exact GUI/playback IDs can join outside the timing window, but the native
  identity observation must belong to the capture lifecycle. An observation
  predating decoder reuse cannot establish ownership of the new generation.
- Respect the existing 512 MiB buffer default and explicit resource limits.
  Report resource loss distinctly from a media transition or corrupt stream.
  Release each recording's memory after successful publication (or a verified
  existing-file match). Keep bytes through publication retries.
  Count distinct new playing instances, including repeats, rather than elapsed
  time or metadata polling. After MaxMatchQueue subsequent media starts (five by default), an unresolved
  recording is retired: with SaveUnmatched=1 (default), save only codec-validated
  complete supported audio under Unmatched in the configured download root,
  with its proper extension and a metadata sidecar. Incomplete, corrupt, or
  unsupported preloads are discarded; with SaveUnmatched=0, release unmatched
  bytes too. Raw DumpStreams output remains a separate opt-in debug tool.
  Failed complete unmatched writes retain their bytes for retry. Pauses,
  buffering, and ordinary updates do not advance the retirement count.
  Completed audio shares the RAM budget with active audio; report pressure
  separately from normal retirement. Account for retained descriptors as well.

## Matcher settings and diagnostics

`[Matcher] TimeOffsetToleranceMs=10000` accepts 0–120000 milliseconds for
conservative relative timing candidates. `DurationToleranceMs=250` accepts
0–2000 milliseconds. `MaxMatchQueue=5` accepts 1–100 subsequent media starts
before unmatched retirement. Metadata duration can differ from measured audio by
about 200 ms, so the default permits that difference. Timing and length filters
remain separate from intrinsic codec integrity: widening either setting cannot
make truncated bytes complete. Exact playback IDs bypass the timing window.
These INI settings take effect after restart. Legacy TimingToleranceSeconds
migrates to TimeOffsetToleranceMs by multiplying by 1000 when the new key is
absent; existing new values take precedence, and the legacy Matcher key is removed.

Debug matching decisions record stream and metadata generation IDs, their
start/end/length values, and signed start, end, and duration deltas with units.
End deltas remain diagnostic only; there is no separate end-time gate.
Late metadata and prefetch make an assumed end relationship unreliable.

## Implementation scope

Replace playback-coverage eligibility as the publication gate. Keep codec
readers, native ownership discovery, and the publication worker. Retire obsolete
natural-end, buffering-hold, and completed-listen recovery branches from the
production flow as their responsibilities move to stream completion and metadata
association. Avoid adding a second competing save path.
Remove or rename elapsed-listen tags such as HISTORY_ELAPSED_SECONDS so capture
time is not presented as listening coverage.

After the complete-only unmatched fix and required checks pass, update and merge
PR #25 and publish the next 1.3 release candidate. Choose its version after
auditing existing releases; this does not authorize a final 1.3 release. Install
and test the playlist live with diagnostics and dumps enabled as applicable.

## Acceptance evidence

- Replay the real failing session and save a validated Queen recording with the
  exact captured Ogg audio.
- Replay prefetched streams, delayed metadata, reordered transitions, repeats,
  and skips without losing or mislabeling a complete stream.
- Preserve recordings across long pauses; reject actual page/sample gaps and
  truncated sources rather than treating elapsed time as data loss.
- Exercise Ogg, FLAC, AAC, and MP3, including a final item without a successor.
- Confirm configured formats, memory limits, duplicate handling, and file-in-use
  retries remain effective.
- Confirm the configured subsequent media start limit saves only complete validated unmatched
  audio, discard incomplete/corrupt preloads, and retain failed complete writes.

The existing dump replay preserves compressed bytes and metadata delivery order,
but reconstructs libogg page/output timing within input callbacks. It proves the
association-order regression, not exact native callback timing. Compare saved
compressed audio byte-for-byte and validate codec structure; use fresh live
dumps for native callback ordering, decoder reuse, and late-tail tests.

Status: user-approved three-stage architecture, configurable timing/length
matching, delta diagnostics, and five-subsequent-song retirement with
SaveUnmatched enabled by default. Implementation and final validation continue.
