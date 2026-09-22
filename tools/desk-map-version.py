#!/usr/bin/env python3
"""Print provenance for the map and the source checkout packaged with the desk."""
import hashlib
import json
import pathlib
import subprocess
import sys


def main():
    root = pathlib.Path(sys.argv[1])
    data = (root / "show/vibra.desk.json").read_bytes()
    show = json.loads(data)["show"]
    head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(["git", "-C", str(root), "status", "--porcelain"]))
    print(f"workspace={show['workspace']} sha256={show['sha256']} repo={head} "
          f"dirty={str(dirty).lower()} map_sha256={hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    main()
