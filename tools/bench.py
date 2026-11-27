#!/usr/bin/env python3
"""Measure command-level Surco and Zstandard on deterministic local data."""
import argparse
import csv
import filecmp
import json
import platform
import re
import statistics
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory
from time import perf_counter_ns

from datasets import PATTERNS, generate


def text(*command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT).strip()


def timed(command, output):
    usage = output.with_name("rss.txt")
    start = perf_counter_ns()
    with output.open("wb") as stream:
        subprocess.run(["/usr/bin/time", "-f", "%M", "-o", usage, "--", *command],
                       stdout=stream, check=True)
    return (perf_counter_ns() - start) / 1e6, int(usage.read_text())


def measurements(values):
    return dict(min=round(min(values), 3), median=round(statistics.median(values), 3),
                max=round(max(values), 3))


def summary(points):
    return dict(ms=measurements([t for t, _ in points]),
                peak_rss_kib=measurements([rss for _, rss in points]))


def workload(surco, directory, pattern, rows, repeats, backends):
    irregular = pattern in {"walk", "noisy", "random"}
    source = directory / "signal.csv"
    with source.open("w", newline="") as stream:
        generate(pattern, rows, irregular, stream)
    query = directory / "filtered.csv"
    lower, upper = rows * 490, rows * 510
    with source.open(newline="") as original, query.open("w", newline="") as filtered:
        reader = csv.reader(original)
        writer = csv.writer(filtered, lineterminator="\n")
        writer.writerow(next(reader))
        writer.writerows(row for row in reader if lower <= int(row[0]) <= upper)
    archive, out, log = (directory / name for name in ("archive.surco", "out.csv", "stdout"))
    raw = directory / "reference.surco"
    subprocess.run([surco, "pack", source, "--raw", "-o", raw], check=True)
    results = {}
    for label in [*backends, "raw"]:
        backend = ("--backend", label if label != "raw" else "auto")
        raw_option = ("--raw",) if label == "raw" else ()
        ops = {"pack": [], "unpack": [], "query": []}
        for _ in range(repeats):
            archive.unlink(missing_ok=True)
            ops["pack"].append(timed([surco, "pack", source, "-o", archive, *backend, *raw_option], log))
            text(surco, "verify", archive, *backend)
            ops["unpack"].append(timed([surco, "unpack", archive, *backend], out))
            if not filecmp.cmp(source, out, shallow=False):
                raise ValueError("unpack round-trip mismatch")
            ops["query"].append(timed([surco, "query", archive, "--from", str(lower),
                                       "--to", str(upper), *backend], out))
            if not filecmp.cmp(query, out, shallow=False):
                raise ValueError("query mismatch")
        meta = json.loads(text(surco, "inspect", archive, "--json", *backend))
        results[label] = dict(bytes=meta["file_bytes"], compressed_blocks=meta["compressed_blocks"],
                              **{key: summary(points) for key, points in ops.items()})

    zs = directory / "reference.surco.zst"
    z_dec = directory / "from-zstd.surco"
    encode, decode = [], []
    for _ in range(repeats):
        encode.append(timed(["zstd", "-q", "-3", "-c", raw], zs))
        decode.append(timed(["zstd", "-q", "-d", "-c", zs], z_dec))
        if not filecmp.cmp(raw, z_dec, shallow=False):
            raise ValueError("Zstandard byte round-trip mismatch")
        timed([surco, "unpack", z_dec], out)
        if not filecmp.cmp(source, out, shallow=False):
            raise ValueError("Zstandard record round-trip mismatch")
    results["zstd_3"] = dict(bytes=zs.stat().st_size, compress=summary(encode), decompress=summary(decode))
    return dict(pattern=pattern, timestamps="irregular" if irregular else "regular", rows=rows,
                numeric_bytes=rows * 16, query_from=lower, query_to=upper, results=results)


def environment(executable):
    build = executable.parent
    cache = (build / "CMakeCache.txt").read_text()
    compiler = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)", cache, re.M)[1]
    nasm = re.search(r"^CMAKE_ASM_NASM_COMPILER:[^=]+=(.+)", cache, re.M)
    release = re.search(r"^CMAKE_C_FLAGS_RELEASE:[^=]+=(.*)", cache, re.M)[1]
    cpu = re.search(r"^model name\s*:\s*(.+)", Path("/proc/cpuinfo").read_text(), re.M)[1]
    return dict(cpu=cpu, system=platform.freedesktop_os_release()["PRETTY_NAME"],
                kernel=platform.release(), compiler=text(compiler, "--version").splitlines()[0],
                build_flags=release + " -std=c17", nasm=text(nasm[1], "-v") if nasm else "disabled",
                lto="SURCO_ENABLE_LTO:BOOL=ON" in cache,
                commit=text("git", "-C", Path(__file__).resolve().parent, "rev-parse", "HEAD"),
                zstandard=text("zstd", "--version"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--surco", default="build/release/surco", type=Path)
    parser.add_argument("--rows", type=int, default=120000)
    parser.add_argument("--repetitions", type=int, default=7)
    parser.add_argument("--scratch", default="build", type=Path)
    args = parser.parse_args()
    if not 1 <= args.repetitions <= 31 or not 1 <= args.rows <= 10_000_000:
        parser.error("invalid rows or repetitions")
    exe = args.surco.resolve()
    backends = ["scalar"]
    if "sse2: available" in text(exe, "doctor"):
        backends.append("sse2")
    output = environment(exe)
    output["repetitions"] = args.repetitions
    output["dataset_seed"] = 734521
    output["transforms"] = [json.loads(text(exe, "benchmark", "--backend", backend,
                                           "--repetitions", str(args.repetitions)))
                            for backend in backends]
    args.scratch.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=args.scratch) as directory:
        root = Path(directory)
        output["datasets"] = []
        for pattern in PATTERNS:
            subdir = root / pattern
            subdir.mkdir()
            output["datasets"].append(workload(exe, subdir, pattern, args.rows, args.repetitions, backends))
    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
