#!/usr/bin/env python3
"""Exercise the build script with real compiler/archive tools in temporary fixtures."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

script = Path(__file__).resolve().parents[1] / "build-artwork-codecs.sh"
with tempfile.TemporaryDirectory(prefix="floggfy-codecs-") as temporary:
    root = Path(temporary)
    build_script = root / script.name
    shutil.copyfile(script, build_script)
    vendor = root / "native/vendor/libwebp"
    (vendor / "src").mkdir(parents=True)
    (vendor / "config.h").write_text("#define VALUE 1\n")
    (vendor / "src/shared.h").write_text('#include "config.h"\n')
    (vendor / "src/first.c").write_text('#include "shared.h"\nint first(void){return VALUE;}\n')
    (vendor / "src/second.c").write_text('int second(void){return 2;}\n')
    (vendor / "sources.txt").write_text("src/first.c\nsrc/second.c\n")
    compiler = root / "cc"
    archiver = root / "ar"
    log = root / "calls"
    for tool, real in ((compiler, shutil.which("gcc")), (archiver, shutil.which("ar"))):
        tool.write_text(f'#!/usr/bin/env bash\nif [[ "$1" != --version ]]; then echo {tool.name} >> "{log}"; fi\nexec "{real}" "$@"\n')
        tool.chmod(0o755)
    output = root / "output with spaces"
    archive = output / "libwebpdecoder.a"

    def run(expected_compiles, expected_archives=1, cc=compiler, ar=archiver):
        log.write_text("")
        subprocess.run(["bash", str(build_script.relative_to(root.parent)), str(cc), str(ar), str(output)],
                       cwd=root.parent, check=True)
        calls = log.read_text().splitlines()
        assert calls.count("cc") == expected_compiles, calls
        assert calls.count("ar") == expected_archives, calls
        assert archive.exists()

    run(2)
    original_mtime = archive.stat().st_mtime_ns
    run(0, 0)
    assert archive.stat().st_mtime_ns == original_mtime
    # Content changes, even with preserved mtimes, must invalidate the build.
    source = vendor / "src/first.c"
    original_stat = source.stat()
    source.write_text(source.read_text().replace("return VALUE", "return VALUE+1"))
    os.utime(source, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))
    run(2)
    (vendor / "src/shared.h").write_text('#include "config.h"\n#define OTHER 1\n')
    run(2)
    (vendor / "config.h").write_text("#define VALUE 3\n")
    run(2)
    # A removed source must disappear from the archive, not survive ar's update mode.
    (vendor / "sources.txt").write_text("src/first.c\n")
    run(1)
    members = subprocess.check_output(["ar", "t", str(archive)], text=True).splitlines()
    assert members == ["src-first.c.o"], members
    (vendor / "sources.txt").write_text("src/first.c\nsrc/second.c\n")
    run(2)
    compiler.write_text(compiler.read_text() + "# replacement compiler wrapper\n")
    run(2)
    archiver.write_text(archiver.read_text() + "# replacement archiver wrapper\n")
    run(2)
    other_cc = root / "other-cc"
    shutil.copyfile(compiler, other_cc)
    other_cc.chmod(0o755)
    run(2, cc=other_cc)
    run(0, 0, cc=other_cc)
    other_ar = root / "other-ar"
    shutil.copyfile(archiver, other_ar)
    other_ar.chmod(0o755)
    run(2, cc=other_cc, ar=other_ar)
    build_script.write_text(build_script.read_text().replace("-O2", "-O1"))
    run(2, cc=other_cc, ar=other_ar)
    archive.unlink()
    run(2, cc=other_cc, ar=other_ar)
    run(0, 0, cc=other_cc, ar=other_ar)
print("PASS: clean/no-op codec builds; source/header/config/list/tool/command invalidation; removed archive members")
