from pathlib import Path
from random import Random
from struct import pack_into, unpack_from
from tempfile import TemporaryDirectory
import json
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / "fixtures"))
from generate import crc

binary = Path(sys.argv[1]).resolve()
fixtures = Path(__file__).resolve().parent / "fixtures"


def cli(*args, data=None, ok=True):
    result = subprocess.run([binary, *map(str, args)], input=data, capture_output=True, timeout=15)
    assert (result.returncode == 0) == ok, (args, result.returncode, result.stderr)
    return result.stdout


def rows_csv(rows, channels=1, unit="us"):
    headline = ",".join([f"timestamp_{unit}", *(f"c{i}" for i in range(channels))])
    return ("\n".join([headline, *(",".join(map(str, row)) for row in rows)]) + "\n").encode()


def meta(path, backend="auto"):
    return json.loads(cli("inspect", path, "--json", "--backend", backend))


def mutated(path, archive, change):
    data = bytearray(archive)
    change(data)
    path.write_bytes(data)
    cli("verify", path, ok=False)


def repair_crc(data, start, span):
    pack_into("<I", data, start + span - 4, crc(data[start:start + span - 4]))


with TemporaryDirectory(dir=Path.cwd()) as tmp:
    tmp = Path(tmp)
    source = tmp / "fixture.surco"
    for file, args in [("raw-v1.surco", ("--raw",)), ("compressed-v1.surco", ())]:
        golden = fixtures / file
        cli("pack", fixtures / "records.csv", "-o", source, *args)
        assert source.read_bytes() == golden.read_bytes()
        assert cli("unpack", source) == (fixtures / "records.csv").read_bytes()
        source.unlink()

    rng = Random(735541)
    rows = [(i // 3, 17, i, -i, rng.randint(-(2**31), 2**31 - 1)) for i in range(4211)]
    wide = [(0, *([-(2**31), 2**31 - 1] * 16)),
            (2**63, *([2**31 - 1, -(2**31)] * 16)),
            (2**64 - 5, *([0] * 32)), (2**64 - 1, *([23] * 32))]
    has_sse2 = b"sse2: available" in cli("doctor")
    for case, channels, unit in [
        ([], 1, "us"), ([(0, 3)], 1, "us"), (rows[:1024], 4, "us"), (rows, 4, "us"),
        (wide, 32, "ns"), ([(i * 1000000, i * 100000) for i in range(300)], 1, "ms"),
        ([(i * i, 18 - i) for i in range(1037)], 1, "s"),
    ]:
        csv_data = rows_csv(case, channels, unit)
        scalar = tmp / "scalar.surco"
        cli("pack", "-", "--backend", "scalar", "-o", scalar, data=csv_data)
        assert cli("unpack", scalar) == csv_data
        assert cli("replay", scalar, "--fast") == csv_data
        assert meta(scalar)["samples"] == len(case)
        cli("verify", scalar, "--backend", "scalar")
        if has_sse2:
            simd = tmp / "simd.surco"
            cli("pack", "-", "--backend", "sse2", "-o", simd, data=csv_data)
            assert simd.read_bytes() == scalar.read_bytes()
            assert cli("unpack", scalar, "--backend", "sse2") == csv_data
            assert cli("unpack", simd, "--backend", "scalar") == csv_data
            simd.unlink()
        for a, b in [(0, 0), (341, 342), (900, 1000), (2**64 - 1, 2**64 - 1)]:
            expected = rows_csv([row for row in case if a <= row[0] <= b], channels, unit)
            assert cli("query", scalar, "--from", a, "--to", b) == expected
        scalar.unlink()

    cli("pack", "-", "-o", source, data=rows_csv(rows, 4))
    assert meta(source)["compressed_blocks"] > 0
    cli("pack", "-", "-o", source, data=rows_csv(rows, 4), ok=False)
    cli("unpack", source, "-o", source, ok=False)
    archive = source.read_bytes()
    cli("recover", source, "-o", source, ok=False)
    assert source.read_bytes() == archive
    header = unpack_from("<I", archive, 12)[0]
    index = unpack_from("<Q", archive, len(archive) - 32)[0]
    second = unpack_from("<Q", archive, index + 32)[0]
    third = unpack_from("<Q", archive, index + 64)[0]

    broken = tmp / "broken.surco"
    mutated(broken, archive, lambda b: b.__setitem__(8, 2))
    mutated(broken, archive, lambda b: b.__setitem__(20, ord("*")))
    def malformed_name(b):
        b[20] = ord("*")
        repair_crc(b, 0, header)
    mutated(broken, archive, malformed_name)
    def malformed_size(b):
        pack_into("<I", b, second + 20, 0xffffffff)
        repair_crc(b, second, 52)
    mutated(broken, archive, malformed_size)
    mutated(broken, archive, lambda b: b.__setitem__(-8, b[-8] ^ 8))
    def mismatched_index(b):
        pack_into("<Q", b, index + 32, second + 1)
        pack_into("<I", b, len(b) - 8, crc(b[index:-36]))
        repair_crc(b, len(b) - 36, 36)
    mutated(broken, archive, mismatched_index)

    mutated(broken, archive, lambda b: b.__setitem__(second + 52, b[second + 52] ^ 1))
    assert cli("query", broken, "--from", 10, "--to", 20) == rows_csv(rows[30:63], 4)
    cli("query", broken, "--from", 342, "--to", 343, ok=False)
    recovered = tmp / "recovered.surco"
    info = json.loads(cli("recover", broken, "-o", recovered))
    assert info == dict(recovered=len(rows) - 1024, lost_at_least=1024,
                       loss_count_known=True, accepted_blocks=4, rejected_payload_blocks=1)
    assert cli("unpack", recovered) == rows_csv(rows[:1024] + rows[2048:], 4)
    recovered.unlink()
    mutated(broken, archive, lambda b: b.__setitem__(second, ord("Z")))
    info = json.loads(cli("recover", broken, "-o", recovered))
    assert info["loss_count_known"] and info["lost_at_least"] == 1024
    assert cli("unpack", recovered) == rows_csv(rows[:1024] + rows[2048:], 4)
    recovered.unlink()
    broken.write_bytes(archive[:third + 55])
    cli("verify", broken, ok=False)
    info = json.loads(cli("recover", broken, "-o", recovered))
    assert not info["loss_count_known"]
    assert cli("unpack", recovered) == rows_csv(rows[:2048], 4)
    recovered.unlink()
    for cut in [0, 7, 19, header - 1, header + 3, len(archive) - 1]:
        broken.write_bytes(archive[:cut])
        cli("verify", broken, ok=False)

    unindexed = tmp / "unindexed.surco"
    unindexed.write_bytes(cli("pack", "-", data=rows_csv(rows, 4)))
    assert not meta(unindexed)["indexed"]
    assert cli("query", unindexed, "--from", 341, "--to", 342) == rows_csv(rows[1023:1029], 4)
    unindexed.unlink()
    cli("pack", "-", "-o", unindexed, "--no-index", data=rows_csv(rows, 4))
    assert not meta(unindexed)["indexed"]
    assert cli("unpack", unindexed) == rows_csv(rows, 4)

    noisy = [(i, rng.randint(-(2**31), 2**31 - 1)) for i in range(1024)]
    fallback = rows_csv([(0, *range(32))], 32)
    raw = tmp / "fallback.surco"
    cli("pack", "-", "-o", raw, data=fallback)
    assert meta(raw)["raw_blocks"] == 1 and cli("unpack", raw) == fallback
    noisy_file = tmp / "noise.surco"
    cli("pack", "-", "-o", noisy_file, data=rows_csv(noisy))
    assert cli("unpack", noisy_file) == rows_csv(noisy)

    for invalid in [b"", b"timestamp_us,c,c\n", b"timestamp_us,c\n2,1\n1,2\n",
                    b"timestamp_us,c\n1,2147483648\n", b"timestamp_us,c\n18446744073709551616,0\n",
                    b"timestamp_us,c\n1,0,3\n", b"timestamp_us,c\n1,\x00\n",
                    b"timestamp_us,c\n1", b"timestamp_us,c\n1," + b"0" * 3000]:
        target = tmp / "failed.surco"
        cli("pack", "-", "-o", target, data=invalid, ok=False)
        assert not target.exists()
    assert cli("benchmark", "--backend", "scalar", "--repetitions", 1)
