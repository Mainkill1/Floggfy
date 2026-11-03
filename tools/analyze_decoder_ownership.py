#!/usr/bin/env python3
"""Fail-closed streaming checks for Floggfy ownership diagnostic records."""

import re
import sys
from pathlib import Path

MAX_LINE = 4096
TOKEN = re.compile(r"^[A-Za-z_][A-Za-z0-9_-]*=[^\s]+$")
PRIVATE = re.compile(r"(?:0x[0-9a-fA-F]+|spotify:|playback[_-]?id|\b(?:address|context|ctx)=)", re.I)


def number(fields, key, line, violations, required=True):
    value = fields.get(key)
    if value is None:
        if required:
            violations.append((line, f"missing {key}"))
        return 0
    try:
        result = int(value, 10)
        if result < 0:
            raise ValueError
        return result
    except ValueError:
        violations.append((line, f"invalid numeric {key}"))
        return 0


def analyze(path):
    captures = {}
    owners = set()
    output_owners = set()
    instances = set()
    used_instances = {}
    violations = []
    events = 0
    loss_generation = 0

    with path.open("r", encoding="utf-8", errors="replace") as source:
        for line_number, raw in enumerate(source, 1):
            if "ownership event=" not in raw:
                continue
            events += 1
            if len(raw) > MAX_LINE:
                violations.append((line_number, "ownership line exceeds bound"))
                continue
            segment = raw[raw.index("ownership event="):].strip()
            if PRIVATE.search(segment):
                violations.append((line_number, "privacy violation in ownership record"))
            fields = {}
            malformed = False
            for item in segment.split()[1:]:
                if not TOKEN.match(item):
                    violations.append((line_number, "malformed ownership token"))
                    malformed = True
                    continue
                key, value = item.split("=", 1)
                if key in fields:
                    violations.append((line_number, f"duplicate field {key}"))
                fields[key] = value
            if malformed and "event" not in fields:
                continue
            event = fields.get("event")
            if not event:
                violations.append((line_number, "missing event"))
                continue

            if event == "capture-begin":
                capture = number(fields, "capture", line_number, violations)
                owner = number(fields, "owner", line_number, violations)
                if not capture or capture in captures:
                    violations.append((line_number, "duplicate or zero capture begin"))
                    continue
                captures[capture] = {
                    "owner": owner, "terminal": False, "output": False,
                    "loss": loss_generation, "line": line_number,
                    "bound": False, "completed": False,
                }
                if owner:
                    owners.add(owner)
            elif event == "output-first":
                owner = number(fields, "owner", line_number, violations)
                if not owner or owner not in owners:
                    violations.append((line_number, "unknown owner output"))
                else:
                    output_owners.add(owner)
                    for capture in captures.values():
                        if capture["owner"] == owner and not capture["terminal"]:
                            capture["output"] = True
            elif event == "playback":
                instance = number(fields, "instance", line_number, violations)
                if not instance:
                    violations.append((line_number, "zero playback instance"))
                else:
                    instances.add(instance)
            elif event == "transition":
                previous = number(fields, "from", line_number, violations)
                following = number(fields, "to", line_number, violations)
                if previous and previous not in instances:
                    violations.append((line_number, "transition from unknown instance"))
                if following:
                    instances.add(following)
            elif event == "binding":
                capture_id = number(fields, "capture", line_number, violations)
                owner = number(fields, "owner", line_number, violations)
                instance = number(fields, "instance", line_number, violations)
                state = number(fields, "state", line_number, violations)
                capture = captures.get(capture_id)
                if not capture:
                    violations.append((line_number, "binding for unknown capture"))
                    continue
                if owner != capture["owner"]:
                    violations.append((line_number, "binding owner mismatch"))
                if state == 2:
                    if not owner or owner not in output_owners:
                        violations.append((line_number, "bound capture lacks output evidence"))
                    if not instance or instance not in instances:
                        violations.append((line_number, "bound capture uses unknown instance"))
                    if loss_generation != capture["loss"]:
                        violations.append((line_number, "binding after loss"))
                    prior = used_instances.get(instance)
                    if prior is not None and prior != capture_id:
                        violations.append((line_number, "playback instance reused by another capture"))
                    used_instances[instance] = capture_id
                    capture["bound"] = True
                if state == 3:
                    unresolved = sum(1 for item in captures.values()
                                     if item["output"] and not item["terminal"])
                    if unresolved > 1:
                        violations.append((line_number, "multiple unresolved successors"))
            elif event in ("capture-complete", "capture-abort"):
                capture_id = number(fields, "capture", line_number, violations)
                owner = number(fields, "owner", line_number, violations)
                capture = captures.get(capture_id)
                if not capture:
                    violations.append((line_number, "terminal event for unknown capture"))
                elif capture["terminal"]:
                    violations.append((line_number, "duplicate terminal event"))
                else:
                    if owner != capture["owner"]:
                        violations.append((line_number, "terminal owner mismatch"))
                    if event == "capture-complete":
                        state = number(fields, "state", line_number, violations)
                        if state not in (1, 2):
                            violations.append((line_number, "completed capture has invalid state"))
                        capture["completed"] = True
                    capture["terminal"] = True
            elif event == "loss":
                loss_generation += 1
            elif event in ("layout", "layout-unavailable", "output-summary"):
                pass
            else:
                violations.append((line_number, f"unknown ownership event {event}"))

    for capture_id, capture in captures.items():
        if not capture["terminal"]:
            violations.append((capture["line"], f"incomplete lifecycle for capture {capture_id}"))
        elif capture["completed"] and not capture["bound"]:
            violations.append((capture["line"], f"completed capture {capture_id} never became bound"))
    return events, captures, owners, instances, violations


def main(argv):
    if len(argv) != 2:
        print("usage: analyze_decoder_ownership.py Floggfy-diagnostics.log", file=sys.stderr)
        return 2
    path = Path(argv[1])
    try:
        events, captures, owners, instances, violations = analyze(path)
    except OSError as error:
        print(f"ownership analysis: cannot read {path}: {error}", file=sys.stderr)
        return 2
    if not events:
        print("ownership analysis: FAIL: no ownership records", file=sys.stderr)
        return 1
    if violations:
        for line, message in violations:
            print(f"{path}:{line}: {message}")
        print(f"ownership analysis: FAIL events={events} captures={len(captures)} "
              f"owners={len(owners)} instances={len(instances)} violations={len(violations)}")
        return 1
    print(f"ownership analysis: PASS events={events} captures={len(captures)} "
          f"owners={len(owners)} instances={len(instances)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
