"""Read Surco metadata through the supported CLI JSON interface."""
import argparse
import json
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("archive")
parser.add_argument("--surco", default="surco")
args = parser.parse_args()
metadata = json.loads(subprocess.check_output([args.surco, "inspect", args.archive, "--json"]))
print(f"{metadata['samples']:,} records on {', '.join(metadata['channels'])}")
