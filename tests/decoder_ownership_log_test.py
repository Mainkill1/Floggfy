#!/usr/bin/env python3
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANALYZER = ROOT / "tools" / "analyze_decoder_ownership.py"


def run(name, lines, expected, needle=""):
    with tempfile.TemporaryDirectory(prefix="floggfy-ownership-") as directory:
        path = Path(directory) / f"{name}.log"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        result = subprocess.run(
            [sys.executable, str(ANALYZER), str(path)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    assert result.returncode == expected, (name, result.returncode, result.stdout)
    if needle:
        assert needle in result.stdout, (name, needle, result.stdout)


def valid_distinct():
    return [
        "x ownership event=layout decoded_rva=1 ogg_delta=-136 flac_delta=0 aac_delta=0 aac_verified=0",
        "x ownership event=capture-begin capture=1 owner=1 codec=flac stream=0 epoch=7 state=0",
        "x ownership event=output-first owner=1 calls=1 produced=80 consumed=10 kept=80 mono=1.0",
        "x ownership event=playback instance=1 uri_changed=0 listen_generation=1 mono=1.1",
        "x ownership event=binding capture=1 owner=1 instance=1 listen_generation=1 state=2",
        "x ownership event=capture-complete capture=1 owner=1 state=2 bytes=100 mono=2.0",
        "x ownership event=capture-begin capture=2 owner=2 codec=ogg stream=41 epoch=7 state=0",
        "x ownership event=output-first owner=2 calls=1 produced=80 consumed=10 kept=80 mono=2.1",
        "x ownership event=transition from=1 to=2 explicit_skip=0 natural=1 mono=2.2",
        "x ownership event=playback instance=2 uri_changed=1 listen_generation=2 mono=2.3",
        "x ownership event=binding capture=2 owner=2 instance=2 listen_generation=2 state=2",
        "x ownership event=capture-complete capture=2 owner=2 state=2 bytes=100 mono=3.0",
    ]


def main():
    valid = valid_distinct()
    run("valid-distinct", valid, 0, "ownership analysis: PASS")
    repeat = [line.replace("uri_changed=1", "uri_changed=0") for line in valid]
    run("valid-same-uri-new-instance", repeat, 0)
    late_binding = [
        valid[0], valid[1], valid[2],
        valid[5].replace("state=2", "state=1"),
        valid[3], valid[4],
    ]
    run("valid-complete-before-binding", late_binding, 0)
    unresolved = valid[:6] + [
        "x ownership event=capture-begin capture=3 owner=3 codec=flac stream=0 epoch=7 state=0",
        "x ownership event=output-first owner=3 calls=1 produced=80 consumed=10 kept=80 mono=4.0",
        "x ownership event=capture-abort capture=3 owner=3 reason=6 state=4",
    ]
    run("valid-unresolved-timeout", unresolved, 0)
    context_reuse = valid[:6] + [line.replace("capture=2", "capture=3").replace("owner=2", "owner=1") for line in valid[6:]]
    run("valid-context-reuse", context_reuse, 0)

    run("missing-begin", [valid[2], valid[3], valid[4], valid[5]], 1, "unknown capture")
    run("duplicate-complete", valid + [valid[5]], 1, "duplicate terminal")
    run("complete-conflict", [*valid[:5], valid[5].replace("state=2", "state=3")],
        1, "completed capture has invalid state")
    run("complete-awaiting-without-binding",
        [valid[0], valid[1], valid[2], valid[5].replace("state=2", "state=1")],
        1, "never became bound")
    run("unknown-owner", [valid[1], valid[2].replace("owner=1", "owner=9")], 1, "unknown owner")
    reused = valid + [
        "x ownership event=capture-begin capture=3 owner=3 codec=flac stream=0 epoch=7 state=0",
        "x ownership event=output-first owner=3 calls=1 produced=80 consumed=10 kept=80 mono=4.0",
        "x ownership event=binding capture=3 owner=3 instance=2 listen_generation=3 state=2",
        "x ownership event=capture-complete capture=3 owner=3 state=2 bytes=100 mono=5.0",
    ]
    run("reused-instance-preload", reused, 1, "instance reused")
    after_loss = valid[:4] + [
        "x ownership event=loss source=media",
        valid[4], valid[5],
    ]
    run("binding-after-loss", after_loss, 1, "binding after loss")
    multiple = valid[:6] + [
        "x ownership event=capture-begin capture=3 owner=3 codec=flac stream=0 epoch=7 state=0",
        "x ownership event=capture-begin capture=4 owner=4 codec=flac stream=0 epoch=7 state=0",
        "x ownership event=output-first owner=3 calls=1 produced=80 consumed=1 kept=80 mono=4",
        "x ownership event=output-first owner=4 calls=1 produced=80 consumed=1 kept=80 mono=4.1",
        "x ownership event=binding capture=3 owner=3 instance=3 listen_generation=3 state=3",
        "x ownership event=binding capture=4 owner=4 instance=0 listen_generation=0 state=3",
        "x ownership event=capture-abort capture=3 owner=3 reason=6 state=4",
        "x ownership event=capture-abort capture=4 owner=4 reason=6 state=4",
    ]
    run("multiple-successors", multiple, 1, "multiple unresolved successors")
    run("incomplete-lifecycle", valid[:-1], 1, "incomplete lifecycle")
    run("raw-address", valid + ["x ownership event=loss source=media address=0x1234"], 1, "privacy")
    run("raw-playback", valid + ["x ownership event=loss source=spotify:track:secret"], 1, "privacy")
    print("PASS: decoder ownership analyzer invariants, lifecycle, loss and privacy failures")


if __name__ == "__main__":
    main()
