from __future__ import annotations

import argparse
from pathlib import Path

from firmware import create_firmware_bundle


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a validated MackoDash full-firmware ZIP.")
    parser.add_argument("build_dir", type=Path, help="ESP-IDF build directory")
    parser.add_argument("output", type=Path, help="Output ZIP path")
    arguments = parser.parse_args()
    output = create_firmware_bundle(arguments.build_dir, arguments.output)
    print(f"Built {output}")


if __name__ == "__main__":
    main()
