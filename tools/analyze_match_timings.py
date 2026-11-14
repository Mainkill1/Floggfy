#!/usr/bin/env python3
"""Summarize observed match deltas; never infer identity or change INI settings.

Reads legacy ``matcher ... decision=...`` and bounded matcher_candidate parts.
Absolute delta statistics use finite known values only, with nearest-rank p95.
Each input file is a separate observed session; concatenate rotated pieces of
one session before passing them. Repeated epoch/capture/metadata/reason records
are one observation using its first known metrics. Later records can fill unknown
fields. Rejected/waiting native paths do not imply a proven identity. Legacy
rejections without a path are reported as unknown. Input labels/paths are never
echoed. These statistics describe observations, not suggested INI tolerances.
"""
import argparse
import json
import math
import re
import sys
from contextlib import ExitStack
from pathlib import Path

MAX_LINE = 4096
RECORD = re.compile(r"\b(?:matcher|matcher_candidate)\s+")
FIELDS = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
DECISIONS = {"matched-native-id": "exact", "matched-unique-evidence": "weak"}
REJECTED = {"metadata-assigned", "metadata-unusable", "outside-start-window", "duration-mismatch",
            "retired-stream-evidence", "retired-metadata-evidence", "ambiguous-streams", "ambiguous-metadata",
            "ownership-excluded", "weak-association-contradicted"}
WAITING = {"waiting-native-identity", "waiting-duration", "binding-pending", "pending"}
UNEVALUATED = {"not-evaluated"}
METRICS = {"first_seen_ms": "first_seen_delta_ms", "start_ms": "start_delta_ms", "duration_ms": "duration_delta_ms"}


def _statistics(values):
    values = sorted(values)
    if not values:
        return {"count": 0, "median": None, "p95": None, "max": None}
    middle = len(values) // 2
    median = values[middle] if len(values) % 2 else values[middle - 1] + (values[middle] - values[middle - 1]) / 2
    return {"count": len(values), "median": median,
            "p95": values[math.ceil(0.95 * len(values)) - 1], "max": values[-1]}


def _summarize(samples, count_name):
    result = {count_name: len(samples)}
    for metric in METRICS:
        result[metric] = _statistics([sample[metric] for sample in samples if metric in sample])
    for check in ("timing", "duration"):
        values = [sample.get(check + "_evaluated") for sample in samples]
        result[check + "_evaluated_count"] = values.count(True)
        result[check + "_unevaluated_count"] = values.count(False)
        result[check + "_unknown_count"] = values.count(None)
    return result


def analyze_sources(sources):
    observations = {}
    coverage = {"input_sessions": 0, "duplicate_matches": 0, "duplicate_observations": 0,
                "oversize_lines": 0, "invalid_identity": 0, "unrecognized_decisions": 0,
                "incomplete_evidence_markers": 0}
    for session, lines in enumerate(sources):
        coverage["input_sessions"] += 1
        for raw in lines:
            if ("matcher_snapshot" in raw and (
                "coverage=omitted" in raw or "truncated=1" in raw or
                re.search(r"(?:metadata_omitted|competitors_omitted|serialized_omitted)=[1-9]\d*",raw))) or (
                "logger diagnostic records omitted=" in raw):
                coverage["incomplete_evidence_markers"]+=1
            marker = RECORD.search(raw)
            if marker is None:
                continue
            if len(raw) > MAX_LINE:
                coverage["oversize_lines"] += 1
                continue
            fields = dict(FIELDS.findall(raw[marker.start():]))
            reason = fields.get("decision")
            if reason not in DECISIONS and reason not in REJECTED | WAITING | UNEVALUATED:
                if reason is not None:
                    coverage["unrecognized_decisions"] += 1
                continue
            try:
                capture, metadata = int(fields["capture"]), int(fields["metadata"])
                epoch = int(fields.get("epoch", "0"))
                if capture <= 0 or metadata <= 0 or epoch < 0:
                    raise ValueError
            except (KeyError, ValueError):
                coverage["invalid_identity"] += 1
                continue
            key = (session, epoch, capture, metadata, reason)
            if key in observations:
                coverage["duplicate_observations"] += 1
                if reason in DECISIONS:
                    coverage["duplicate_matches"] += 1
            sample = observations.setdefault(key, {"path": "unknown"})
            path = fields.get("path", "unknown")
            if sample["path"] == "unknown" and path in ("native", "weak"):
                sample["path"] = path
            for check in ("timing_evaluated", "duration_evaluated"):
                if check not in sample and fields.get(check) in ("0", "1"):
                    sample[check] = fields[check] == "1"
            for metric, field in METRICS.items():
                if metric in sample:
                    continue
                try:
                    value = float(fields[field])
                except (KeyError, ValueError):
                    continue
                if math.isfinite(value):
                    sample[metric] = abs(value)
    result = {"coverage": coverage, "rejected": {}, "waiting": {}, "unevaluated": {}}
    matched = {"exact": [], "weak": []}
    categorized = {}
    for key, sample in observations.items():
        reason = key[-1]
        if reason in DECISIONS:
            matched[DECISIONS[reason]].append(sample)
            continue
        category = "rejected" if reason in REJECTED else "waiting" if reason in WAITING else "unevaluated"
        categorized.setdefault((category, reason, sample["path"]), []).append(sample)
    for group, samples in matched.items():
        result[group] = _summarize(samples, "matches")
    for (category, reason, path), samples in sorted(categorized.items()):
        result[category].setdefault(reason, {})[path] = _summarize(samples, "observations")
    return result


def analyze_lines(lines):
    return analyze_sources([lines])


def _print_metrics(path, reason, sample):
    for metric in METRICS:
        values = ["unknown" if sample[metric][key] is None else f"{sample[metric][key]:.3f}" for key in ("median", "p95", "max")]
        print(f"{path:7} {reason:27} {metric:15} {sample[metric]['count']:5} {values[0]:>11} {values[1]:>11} {values[2]:>11}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", help="one observed session per file; omit or use - for stdin")
    parser.add_argument("--json", action="store_true", help="emit machine-readable statistics")
    args = parser.parse_args(argv)
    with ExitStack() as stack:
        sources = [sys.stdin if path == "-" else stack.enter_context(Path(path).open(encoding="utf-8", errors="replace"))
                   for path in (args.logs or ["-"])]
        report = analyze_sources(sources)
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print("Absolute deltas (ms); finite known values; nearest-rank p95; descriptive observations only")
        print("path    reason                      metric          count      median         p95         max")
        for group in ("exact", "weak"):
            _print_metrics(group, "matched", report[group])
        for category in ("rejected", "waiting", "unevaluated"):
            for reason, paths in report[category].items():
                for path, sample in paths.items():
                    _print_metrics(path, reason, sample)
        print("Coverage: " + ", ".join(f"{key}={value}" for key, value in report["coverage"].items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
