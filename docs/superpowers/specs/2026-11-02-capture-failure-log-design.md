# Capture Failure Log Design

## Intent

Make delayed cache-cleanup failures short and attributable to the capture that actually failed.

## Behavior

- The activity log uses `failed incomplete cache | Artist - Title` when incomplete cached audio is released.
- A capture snapshots artist and title only when exact decoder ownership binds it to a listen.
- Unknown ownership uses `unknown track`; it never borrows the currently playing title.
- Byte counts, capture IDs, ownership states, and release reasons remain in the diagnostics log.

## Scope

This changes logging only. Capture, ownership, validation, retention, and publication decisions remain unchanged.

## Approach

Store the bound display identity on `Capture`, then split the compact activity outcome from the existing detailed diagnostic evidence. Looking up the current media was rejected because delayed cleanup can occur during a later song; looking up discarded listen history was rejected because that history may no longer be retained.
