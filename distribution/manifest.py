#!/usr/bin/env python3
"""Generate the release installer's checksum manifest from a built ARM64 deb."""
import hashlib
import json
import pathlib
import re
import subprocess
import sys


def main():
    package = pathlib.Path(sys.argv[1])
    version = sys.argv[2].removeprefix("v")
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        sys.exit("version must look like v1.2.3")
    for field, expected in (("Package", "pi-control"), ("Version", version), ("Architecture", "arm64")):
        actual = subprocess.check_output(["dpkg-deb", "-f", str(package), field], text=True).strip()
        if actual != expected:
            sys.exit(f"package {field}: expected {expected}, got {actual}")
    if package.name != f"pi-control_{version}_arm64.deb":
        sys.exit("unexpected package filename")
    print(json.dumps({"schema": 1, "version": version,
                      "sha256": {"linux-aarch64": hashlib.sha256(package.read_bytes()).hexdigest()}}, indent=2))


if __name__ == "__main__":
    main()
