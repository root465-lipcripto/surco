#!/usr/bin/env python3
"""Compare CSV schema and integer records without loading either file."""
import argparse
import csv
from itertools import zip_longest
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("original", type=Path)
parser.add_argument("restored", type=Path)
args = parser.parse_args()
with args.original.open(newline="") as src, args.restored.open(newline="") as dst:
    originals, restored = csv.reader(src), csv.reader(dst)
    header = next(originals, None)
    if header is None or header != next(restored, None):
        parser.exit(1, "schema mismatch\n")
    count = 0
    for count, (a, b) in enumerate(zip_longest(originals, restored), 1):
        try:
            match = (a is not None and b is not None and len(a) == len(b) == len(header)
                     and list(map(int, a)) == list(map(int, b)))
        except ValueError:
            match = False
        if not match:
            parser.exit(1, f"numeric mismatch at CSV line {count + 1}\n")
print(f"Matched {count} records and schema")
