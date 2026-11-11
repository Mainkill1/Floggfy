# Large Podcast Defaults Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans.

**Goal:** Match the saved INI template and support a configurable 1 GiB capture limit with a 512 MiB default.

**Architecture:** Share default/max bounds across settings, MP3 parsing and recording I/O; retain codec-specific budgets.

**Tech Stack:** C++17, MinGW, portable scanner fixtures and Windows file tests.

**Spec:** ../specs/2026-11-11-large-podcast-defaults.md

## Tasks

- [x] Observe new-default and large-MP3 regressions fail against the old behavior.
- [x] Copy the saved INI wording and align runtime defaults, including Podcasts=1.
- [x] Set default 512/max 1024 MiB and align the large-podcast parser/read/publication path.
- [x] Verify virtual parser boundaries and a real Windows file above 512 MiB.
- [x] Complete portable, Windows, sanitizer and build checks; package and record evidence.

## Execution ledger

The parser initially rejected 1,000,001 frames. The new Windows default assertion
initially rejected Podcasts=1. Both regressions passed after implementation.
The live saved template changed MaxBufferedMiB to 512 during work; copied that
latest template exactly and verified text equality. A Windows settings binary
built before the template copy retained 500; rebuilding after synchronization
passed all six focused settings scenarios.

The optional Windows gate successfully tagged, published, inspected, renamed
and reused a 536,872,012-byte synthetic MP3. No user recording was used. Static
review found no important introduced defects; the separate pre-existing 500 MiB
metadata-update cap does not apply to podcasts, which skip that path.

The full Windows invocation reached the settings failure gate and exposed a
pre-existing test race: its baseline snapshot could run before startup migration
persisted. Added FlushSettings before injecting the new fault; the focused gate
passed. Unchanged preceding gates had passed; all remaining Windows wrapper
gates passed in sequence without repeating those unaffected checks. The final
package INI, DLL and internal/external checksums were verified.
