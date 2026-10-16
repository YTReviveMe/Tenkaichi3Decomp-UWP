#!/usr/bin/env python3
"""Build the relocatable state map from the MSVC link map."""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
LINK_MAP = ROOT / "build/uwp-playable/Tenkaichi3DecompUWP.map"
OUTPUT = ROOT / "port/build/uwp/pack/Tenkaichi3DecompUWP.exe.mem"

PORT_STATE = {
    "headless", "mathf_pc", "plat_file", "plat_libm", "plat_mc", "plat_mem",
    "plat_sndstate", "plat_stub", "plat_sys", "softfloat_ps2", "vu0_a",
    "vu0_b", "gs_marker",
}


def is_game(obj: str) -> bool:
    name = obj.lower().split(":")[-1]
    if name.startswith("cod_") or name.startswith("src_"):
        return True
    if name.startswith("port_src_") and name.endswith(".obj"):
        return name[9:-4] in PORT_STATE
    return False


def main() -> None:
    text = LINK_MAP.read_text(errors="replace")
    match = re.search(r"Preferred load address is ([0-9a-fA-F]+)", text)
    if match is None:
        raise SystemExit("MSVC map has no preferred image base")
    preferred = int(match.group(1), 16)

    sections = [
        (int(offset, 16), int(size, 16))
        for offset, size, name in re.findall(
            r"^ 0003:([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})H\s+(\S+)", text, re.M
        )
        if name.startswith((".data", ".bss", ".sdata", ".sbss"))
    ]
    if not sections:
        raise SystemExit("MSVC map has no writable image segment")
    segment_end = max(offset + size for offset, size in sections)

    symbols = {}
    pattern = re.compile(
        r"^ 0003:([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{16})\s+(\S+)\s*$",
        re.M,
    )
    segment_rva = None
    marks = {}
    for m in pattern.finditer(text):
        offset, name, absolute, obj = m.groups()
        offset, absolute = int(offset, 16), int(absolute, 16)
        if offset >= segment_end or absolute < preferred:
            continue
        rva = absolute - preferred
        base = rva - offset
        if segment_rva is None:
            segment_rva = base
        elif base != segment_rva:
            raise SystemExit("MSVC map has inconsistent writable segment RVAs")
        game = is_game(obj)
        if offset in symbols and symbols[offset] != game:
            raise SystemExit(f"Game and host symbols share writable offset {offset:x}")
        symbols[offset] = game
        if name in ("gPortResim", "gPortVBlanks"):
            marks[name] = rva

    if segment_rva is None or len(marks) != 2 or not symbols:
        raise SystemExit("MSVC map is missing the state markers or writable symbols")

    offsets = sorted(symbols)
    runs = []
    for index, start in enumerate(offsets):
        if not symbols[start]:
            continue
        end = offsets[index + 1] if index + 1 < len(offsets) else segment_end
        if end <= start:
            continue
        if runs and runs[-1][0] + runs[-1][1] == segment_rva + start:
            runs[-1] = (runs[-1][0], runs[-1][1] + end - start)
        else:
            runs.append((segment_rva + start, end - start))

    if not runs or len(runs) > 62:
        raise SystemExit(f"Unexpected state region count: {len(runs)}")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(
        f"0 {marks['gPortResim']:x}\n1 {marks['gPortVBlanks']:x}\n"
        + "".join(f"{start:x} {length:x}\n" for start, length in runs),
        encoding="ascii",
    )
    print(f"UWP state map: {len(runs)} ranges, {sum(length for _, length in runs):,} bytes")
    print(OUTPUT)


if __name__ == "__main__":
    main()
