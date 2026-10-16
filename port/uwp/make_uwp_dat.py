#!/usr/bin/env python3
"""Build the relocatable data map from the MSVC link map."""

import pathlib
import re
import struct
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
MAP = ROOT / "build/uwp-playable/Tenkaichi3DecompUWP.map"
DATA = ROOT / "port/build/uwp/data"
OUTPUT = ROOT / "port/build/uwp/pack/Tenkaichi3DecompUWP.dat"


def main():
    text = MAP.read_text(errors="replace")
    preferred = int(re.search(r"Preferred load address is ([0-9A-Fa-f]+)", text).group(1), 16)
    placed = {}
    pattern = re.compile(r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{16})\s+(\S+\.obj)\s*$", re.M)
    for match in pattern.finditer(text):
        name, address, obj = match.groups()
        placed.setdefault(pathlib.Path(obj).name.lower(), {})[name] = int(address, 16) - preferred
    index, checksum = {}, None
    for line in (ROOT / "port/data/index.txt").read_text().splitlines():
        parts = line.split()
        if len(parts) == 3 and not line.startswith("#"):
            index[parts[0]] = int(parts[1]), int(parts[2], 16)
        elif len(parts) == 2 and parts[0] == "checksum":
            checksum = int(parts[1], 16)
    if checksum is None:
        raise SystemExit("port/data/index.txt has no checksum")
    records = []
    for stem, (source, source_offset) in sorted(index.items()):
        obj = DATA / (stem + ".obj")
        symbols = placed.get(obj.name.lower(), {})
        nm = subprocess.run(["llvm-nm", "-n", "--defined-only", str(obj)],
                            check=True, capture_output=True, text=True)
        bases = set()
        for line in nm.stdout.splitlines():
            match = re.match(r"^([0-9a-fA-F]+)\s+[DBR]\s+(\S+)$", line)
            if match and match.group(2) in symbols:
                bases.add(symbols[match.group(2)] - int(match.group(1), 16))
        if len(bases) != 1:
            raise SystemExit(f"{stem}: could not determine one section RVA from {len(bases)} bases")
        base = bases.pop()
        offset = 0
        for line in (ROOT / "port/data" / (stem + ".s")).read_text().splitlines():
            parts = line.split()
            if not parts:
                continue
            if parts[0] == ".space":
                size = int(parts[1], 0)
                records.append((base + offset, source, source_offset + offset, size))
                offset += size
            elif parts[0] == ".zero":
                offset += int(parts[1], 0)
            elif parts[0] == ".long":
                offset += 4
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(struct.pack("<4sIQ", b"BT3D", len(records), checksum) +
                       b"".join(struct.pack("<IIII", *record) for record in records))
    print(f"UWP data manifest: {len(index)} tables, {len(records)} runs, "
          f"{sum(record[3] for record in records)} bytes to load from the user's game files")
    print(OUTPUT)


if __name__ == "__main__":
    main()
