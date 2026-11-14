import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("analyze_match_timings", Path(__file__).parents[1] / "tools/analyze_match_timings.py")
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)


class MatcherTimingTests(unittest.TestCase):
    def test_old_and_new_records_split_exact_and_weak_and_deduplicate(self):
        report = ANALYZER.analyze_lines([
            "12:00 matcher capture=1 metadata=2 decision=matched-native-id first_seen_delta_ms=-100 start_delta_ms=0 duration_delta_ms=-5\n",
            "matcher_candidate snapshot=5 capture=1 metadata=2 decision=matched-native-id first_seen_delta_ms=-100 start_delta_ms=0 duration_delta_ms=-5\n",
            "matcher_candidate snapshot=6 capture=3 metadata=4 decision=matched-unique-evidence first_seen_delta_ms=200 start_delta_ms=-40 duration_delta_ms=unknown end_delta_ms=unknown\n",
            "matcher_candidate snapshot=7 capture=5 metadata=6 decision=duration-mismatch first_seen_delta_ms=999 start_delta_ms=999 duration_delta_ms=999\n",
        ])
        self.assertEqual(report["exact"]["matches"], 1)
        self.assertEqual(report["weak"]["matches"], 1)
        self.assertEqual(report["exact"]["first_seen_ms"], {"count": 1, "median": 100.0, "p95": 100.0, "max": 100.0})
        self.assertEqual(report["exact"]["start_ms"]["median"], 0)
        self.assertEqual(report["weak"]["start_ms"]["max"], 40)
        self.assertEqual(report["weak"]["duration_ms"]["count"], 0)
        self.assertIsNone(report["weak"]["duration_ms"]["median"])
        self.assertEqual(report["coverage"]["duplicate_matches"], 1)

    def test_nearest_rank_p95_and_epoch_separation(self):
        lines = [f"matcher capture={i} metadata=1 decision=matched-native-id first_seen_delta_ms={-i} start_delta_ms={i} duration_delta_ms={i}\n" for i in range(1, 21)]
        lines.append("matcher_candidate epoch=1 capture=1 metadata=1 decision=matched-native-id first_seen_delta_ms=unknown start_delta_ms=unknown duration_delta_ms=unknown\n")
        report = ANALYZER.analyze_lines(lines)
        self.assertEqual(report["exact"]["matches"], 21)
        self.assertEqual(report["exact"]["first_seen_ms"], {"count": 20, "median": 10.5, "p95": 19.0, "max": 20.0})

    def test_cli_reads_fixture_and_leaves_ini_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / "fixture.log"
            fixture.write_text("matcher_candidate epoch=2 capture=7 metadata=8 decision=matched-unique-evidence first_seen_delta_ms=-25 start_delta_ms=5 duration_delta_ms=0\n")
            ini = root / "ToDisk.ini"
            ini.write_text("[Matcher]\nDurationToleranceMs=250\n")
            before = ini.read_bytes()
            result = subprocess.run([sys.executable, str(SPEC.origin), "--json", str(fixture)], check=True, capture_output=True, text=True)
            self.assertEqual(json.loads(result.stdout)["weak"]["duration_ms"]["max"], 0)
            self.assertEqual(ini.read_bytes(), before)

    def test_rejected_and_waiting_reasons_keep_delta_statistics_separate(self):
        report = ANALYZER.analyze_lines([
            "matcher_candidate epoch=1 capture=1 metadata=2 decision=duration-mismatch path=weak timing_evaluated=1 duration_evaluated=1 first_seen_delta_ms=-40 start_delta_ms=-20 duration_delta_ms=300\n",
            "matcher_candidate epoch=1 capture=1 metadata=2 decision=duration-mismatch path=weak timing_evaluated=1 duration_evaluated=1 first_seen_delta_ms=-40 start_delta_ms=-20 duration_delta_ms=300\n",
            "matcher_candidate epoch=1 capture=1 metadata=2 decision=waiting-native-identity path=native timing_evaluated=0 duration_evaluated=0 first_seen_delta_ms=-40 start_delta_ms=-20 duration_delta_ms=unknown\n",
            "matcher capture=3 metadata=4 decision=outside-start-window first_seen_delta_ms=-12000 start_delta_ms=-11000 duration_delta_ms=10\n",
            "matcher_candidate capture=3 metadata=4 decision=not-evaluated path=weak duration_delta_ms=0\n",
        ])
        self.assertIn("duration-mismatch", report.get("rejected", {}))
        rejected = report["rejected"]["duration-mismatch"]["weak"]
        self.assertEqual(rejected["observations"], 1)
        self.assertEqual(rejected["duration_ms"]["median"], 300)
        self.assertEqual(rejected["duration_evaluated_count"], 1)
        self.assertEqual(report["waiting"]["waiting-native-identity"]["native"]["duration_ms"]["count"], 0)
        self.assertEqual(report["rejected"]["outside-start-window"]["unknown"]["start_ms"]["max"], 11000)
        self.assertIn("not-evaluated", report["unevaluated"])
        self.assertEqual(report["exact"]["matches"], 0)
        self.assertEqual(report["weak"]["matches"], 0)

    def test_cli_namespaces_session_ids_per_input_file(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / "first.log", Path(directory) / "second.log"]
            for path in paths:
                path.write_text("matcher capture=1 metadata=1 decision=matched-native-id first_seen_delta_ms=10 start_delta_ms=0 duration_delta_ms=0\n")
            result = subprocess.run([sys.executable, str(SPEC.origin), "--json", *map(str, paths)], check=True, capture_output=True, text=True)
            self.assertEqual(json.loads(result.stdout)["exact"]["matches"], 2)

    def test_unknown_nonfinite_missing_malformed_and_oversize_records(self):
        report = ANALYZER.analyze_lines([
            "matcher capture=1 metadata=1 decision=matched-native-id first_seen_delta_ms=nan start_delta_ms=inf duration_delta_ms=wat\n",
            "matcher capture=2 metadata=2 decision=matched-native-id\n",
            "matcher_candidate capture=2 metadata=2 decision=matched-native-id first_seen_delta_ms=12 start_delta_ms=unknown duration_delta_ms=0\n",
            "matcher capture=1 metadata=1 decision=matched-native-id " + "x" * 5000,
            "not a matcher log spotify:track:private C:/Users/private\n",
        ])
        self.assertEqual(report["exact"]["matches"], 2)
        self.assertEqual(report["exact"]["first_seen_ms"]["count"], 1)
        self.assertEqual(report["exact"]["duration_ms"]["median"], 0)
        self.assertEqual(report["coverage"]["oversize_lines"], 1)
        self.assertNotIn("private", str(report))


if __name__ == "__main__":
    unittest.main()
