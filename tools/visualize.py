#!/usr/bin/env python3
"""Export a verified Surco recording as a self-contained, offline HTML plot."""
import argparse
import csv
import json
from pathlib import Path
import subprocess


def plot_data(exe, source, max_points, synthetic):
    metadata = json.loads(subprocess.check_output([exe, "inspect", source, "--json"]))
    subprocess.run([exe, "verify", source], check=True, stdout=subprocess.DEVNULL)
    points = []
    count = metadata["samples"]
    selected = min(max_points, count)
    next_row = 0
    with subprocess.Popen([exe, "unpack", source], stdout=subprocess.PIPE, text=True) as process:
        rows = csv.reader(process.stdout)
        next(rows, None)
        for index, row in enumerate(rows):
            if len(points) < selected and index == next_row:
                points.append([row[0], *map(int, row[1:])])
                if selected > 1:
                    next_row = len(points) * (count - 1) // (selected - 1)
        if process.wait() or len(points) != selected:
            raise ValueError("Archive changed or failed during export")
    for field in ("first", "last"):
        metadata[field] = str(metadata[field])
    return dict(meta=metadata, points=points, synthetic=synthetic, filename=source.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--surco", default="surco")
    parser.add_argument("--max-points", type=int, default=900)
    parser.add_argument("--synthetic", action="store_true", help="label a known synthetic recording")
    args = parser.parse_args()
    if not 2 <= args.max_points <= 5000:
        parser.error("--max-points must be 2..5000")
    template = Path(__file__).with_name("visualizer.html").read_text(encoding="utf-8")
    payload = json.dumps(plot_data(args.surco, args.archive, args.max_points, args.synthetic),
                         separators=(",", ":")).replace("<", "\\u003c")
    with args.output.open("x", encoding="utf-8") as stream:
        stream.write(template.replace("SURCO_DATA", payload, 1))


if __name__ == "__main__":
    main()
