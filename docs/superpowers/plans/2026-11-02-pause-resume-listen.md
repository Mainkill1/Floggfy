# Pause/Resume Listen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve complete-listen eligibility across observed stationary pauses without weakening gap or seek rejection.

**Architecture:** Adjust the core listen timeline gap guard only when its prior state is paused. Exercise the production `Listen::Observe` state machine directly at 1× and 10×.

**Tech Stack:** C++17 portable native tests

**Spec:** `docs/superpowers/specs/2026-11-02-pause-resume-listen-design.md`

## Global Constraints

- Pause time never advances listening coverage.
- Unobserved gaps while playing remain invalid after three seconds.
- Forward/backward seeks retain existing rejection or restart behavior.
- No capture, ownership, or publication code changes.

## Review Focus

- Repeated paused polls separated by more than three seconds.
- Resume with small clock/sample drift at 10×.
- Forward seek during a pause.
- Seek to the beginning during a pause.
- A long playing gap with unchanged position must remain invalid.

---

### Task 1: Make listen gaps pause-aware

**Files:**
- Modify: `native/ogg_history_core.cpp`
- Test: `tests/history_core_test.cpp`

**Interfaces:**
- Consumes: `Listen::playing`, `last_time`, `last_position`, and existing continuity tolerances.
- Produces: pause-aware eligibility through `Listen::Observe` with no public API change.

- [ ] **Step 1: Add failing pause/resume regressions**

Cover a long 1× pause followed by natural completion, a long 10× pause/resume with small drift, a paused forward seek, a paused seek-to-start, and an unexplained long playing gap.

- [ ] **Step 2: Run `history_core_test` and verify the long-pause cases fail**

- [ ] **Step 3: Permit long elapsed intervals only when the previous state is paused**

Keep the existing position, duration, generation, and seek guards unchanged.

- [ ] **Step 4: Run focused and full native/Windows verification and production build**

- [ ] **Step 5: Commit, independently review, update PR #21/RC1, and install the exact build**
