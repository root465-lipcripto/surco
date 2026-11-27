from pathlib import Path
import os
import pty
import signal
import subprocess
import sys
import termios
import time
from tempfile import TemporaryDirectory

binary = Path(sys.argv[1]).resolve()


def capture(directory, name, more, frames, finish=None):
    master, slave = pty.openpty()
    previous = termios.tcgetattr(slave)
    file = directory / name
    try:
        with subprocess.Popen(
            [binary, "capture", "--device", os.ttyname(slave), "-o", file, *more],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        ) as child:
            deadline = time.monotonic() + 3
            while termios.tcgetattr(slave)[3] & termios.ICANON:
                if time.monotonic() > deadline or child.poll() is not None:
                    child.kill()
                    raise AssertionError(child.communicate())
                time.sleep(0.005)
            for frame in frames:
                os.write(master, frame)
            if finish:
                time.sleep(0.1)
                child.send_signal(finish)
            stdout, stderr = child.communicate(timeout=5)
            assert child.returncode == 0, (stdout, stderr)
            assert termios.tcgetattr(slave) == previous
        decoded = subprocess.check_output([binary, "unpack", file])
        return decoded, stderr
    finally:
        os.close(slave)
        os.close(master)


with TemporaryDirectory(dir=Path.cwd()) as tmp:
    tmp = Path(tmp)
    output, info = capture(
        tmp, "serial.surco", ["--channels", "2", "--samples", "2"],
        [b"broken\n", b"1,0,-10\r", b"\n1,1,20\n", b"x" * 150 + b"\n",
         b"2,0,30\n", b"3,1,40\n", b"4,0,50\n4,1,-60\n"],
    )
    assert output == b"timestamp_us,channel_0,channel_1\n1,-10,20\n4,50,-60\n"
    assert b"3 invalid or incomplete frames" in info
    output, info = capture(tmp, "idle.surco", ["--timeout-ms", "150"], [b"bad\n"])
    assert output == b"timestamp_us,channel_0\n" and b"idle timeout" in info
    output, info = capture(tmp, "signal.surco", [], [b"77,0,12\n"], signal.SIGINT)
    assert output.endswith(b"77,12\n") and b"(signal)" in info
