#!/usr/bin/env python3
"""Generate reproducible two-channel CSV datasets for Surco."""
import argparse
import csv
import random
import sys

PATTERNS = ("constant", "walk", "periodic", "noisy", "random")


def generate(pattern, samples, irregular, output, seed=734521):
    rng = random.Random(seed)
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow(("timestamp_us", "primary", "reference"))
    timestamp = 0
    x, y = 50, -25
    for index in range(samples):
        phase = index % 100
        wave = (phase if phase < 50 else 100 - phase) * 90 - 2000
        if pattern == "walk":
            x += rng.randint(-3, 3)
            y += rng.randint(-2, 2)
        elif pattern == "periodic":
            x, y = wave, (index % 43) * 32 - 900
        elif pattern == "noisy":
            x, y = wave + rng.randint(-150, 150), rng.randint(-500, 500)
        elif pattern == "random":
            x = rng.randint(-(2**31), 2**31 - 1)
            y = rng.randint(-(2**31), 2**31 - 1)
        writer.writerow((timestamp, x, y))
        timestamp += rng.randint(0, 2000) if irregular else 1000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pattern", choices=PATTERNS)
    parser.add_argument("--rows", type=int, default=10000)
    parser.add_argument("--irregular", action="store_true")
    parser.add_argument("--seed", type=int, default=734521)
    args = parser.parse_args()
    if not 0 <= args.rows <= 100_000_000:
        parser.error("--rows must be 0..100000000")
    generate(args.pattern, args.rows, args.irregular, sys.stdout, args.seed)


if __name__ == "__main__":
    main()
