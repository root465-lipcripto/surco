"""A file much larger than the working-set limit still round-trips."""
from pathlib import Path
import resource
import subprocess
import sys
from tempfile import TemporaryDirectory


def limit_memory():
    cap = 32 * 1024 * 1024
    resource.setrlimit(resource.RLIMIT_AS, (cap, cap))


binary = str(Path(sys.argv[1]).resolve())
with TemporaryDirectory(dir=Path.cwd()) as directory:
    dst = Path(directory) / "large.surco"
    process = subprocess.Popen(
        [binary, "pack", "-", "--raw", "-o", dst], stdin=subprocess.PIPE,
        preexec_fn=limit_memory,
    )
    with process.stdin as pipe:
        pipe.write(b"timestamp_us,c0,c1,c2,c3,c4,c5,c6,c7\n")
        chunk = b"1,1,2,3,4,5,6,7,8\n" * 1000
        for _ in range(1200):
            pipe.write(chunk)
    assert process.wait(timeout=35) == 0
    assert dst.stat().st_size > 32 * 1024 * 1024
    verified = subprocess.check_output([binary, "verify", dst], preexec_fn=limit_memory)
    assert b"1200000 samples" in verified
    query = subprocess.check_output(
        [binary, "query", dst, "--from", "2"], preexec_fn=limit_memory,
    )
    assert query == b"timestamp_us,c0,c1,c2,c3,c4,c5,c6,c7\n"
