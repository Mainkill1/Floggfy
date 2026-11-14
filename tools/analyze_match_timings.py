#!/usr/bin/env python3
"""Summarize observed match deltas; never infer identity or change INI settings.

Reads legacy ``matcher ... decision=...`` and bounded matcher_candidate parts.
Absolute delta statistics use finite known values only, with nearest-rank p95.
Repeated records for one epoch/capture/metadata/path are one match. Later records
can fill initially unknown fields. Input labels and paths are never echoed.
"""
import argparse
import json
import math
import re
import sys
from contextlib import ExitStack
from itertools import chain
from pathlib import Path

MAX_LINE = 4096
RECORD = re.compile(r"\b(?:matcher|matcher_candidate)\s+")
FIELDS = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
DECISIONS = {"matched-native-id": "exact", "matched-unique-evidence": "weak"}
METRICS = {"first_seen_ms": "first_seen_delta_ms", "start_ms": "start_delta_ms", "duration_ms": "duration_delta_ms"}


def _statistics(values):
    values = sorted(values)
    if not values:
        return {"count": 0, "median": None, "p95": None, "max": None}
    middle = len(values) // 2
    median = values[middle] if len(values) % 2 else values[middle - 1] + (values[middle] - values[middle - 1]) / 2
    return {"count": len(values), "median": median,
            "p95": values[math.ceil(0.95 * len(values)) - 1], "max": values[-1]}


def analyze_lines(lines):
    matches = {"exact": {}, "weak": {}}
    coverage = {"duplicate_matches": 0, "oversize_lines": 0, "invalid_identity": 0}
    for raw in lines:
        marker = RECORD.search(raw)
        if marker is None:
            continue
        if len(raw) > MAX_LINE:
            coverage["oversize_lines"] += 1
            continue
        fields = dict(FIELDS.findall(raw[marker.start():]))
        group = DECISIONS.get(fields.get("decision"))
        if group is None:
            continue
        try:
            capture, metadata = int(fields["capture"]), int(fields["metadata"])
            epoch = int(fields.get("epoch", "0"))
            if capture <= 0 or metadata <= 0 or epoch < 0:
                raise ValueError
        except (KeyError, ValueError):
            coverage["invalid_identity"] += 1
            continue
        key = (epoch, capture, metadata)
        if key in matches[group]:
            coverage["duplicate_matches"] += 1
        sample = matches[group].setdefault(key, {})
        for metric, field in METRICS.items():
            if metric in sample:
                continue
            try:
                value = float(fields[field])
            except (KeyError, ValueError):
                continue
            if math.isfinite(value):
                sample[metric] = abs(value)
    result = {"coverage": coverage}
    for group, samples in matches.items():
        result[group] = {"matches": len(samples)}
        for metric in METRICS:
            result[group][metric] = _statistics([sample[metric] for sample in samples.values() if metric in sample])
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", help="log files; omit or use - for stdin")
    parser.add_argument("--json", action="store_true", help="emit machine-readable statistics")
    args = parser.parse_args(argv)
    with ExitStack() as stack:
        sources = [sys.stdin if path == "-" else stack.enter_context(Path(path).open(encoding="utf-8", errors="replace"))
                   for path in (args.logs or ["-"])]
        report = analyze_lines(chain.from_iterable(sources))
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print("Absolute matched deltas (ms); finite known values; nearest-rank p95")
        print("path    metric          count      median         p95         max")
        for group in ("exact", "weak"):
            for metric in METRICS:
                sample = report[group][metric]
                values = ["unknown" if sample[key] is None else f"{sample[key]:.3f}" for key in ("median", "p95", "max")]
                print(f"{group:7} {metric:15} {sample['count']:5} {values[0]:>11} {values[1]:>11} {values[2]:>11}")
        print("Coverage: " + ", ".join(f"{key}={value}" for key, value in report["coverage"].items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
