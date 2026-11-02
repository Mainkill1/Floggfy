# Pause/Resume Listen Design

## Intent

Keep a verified listen eligible through a real pause, including pauses longer than three seconds and accelerated capture.

## Behavior

- A long interval is allowed when the previous observation was paused and position remains within the existing continuity tolerance.
- Resume continues the same generation and coverage; pause time does not count as listening progress.
- Playing observation gaps, unobserved pauses, duration changes, and seeks remain rejected or restart coverage under existing rules.

## Approach

Make the observation-gap guard conditional on the previous playing state. The existing position and duration guards remain authoritative, so a pause is not a way to hide a seek.
