#!/usr/bin/env python3
"""Generate trap stubs for unresolved game-library symbols."""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
LOG = ROOT / "build/uwp-playable/link.log"
OUT = ROOT / "port/build/uwp/gen/stubs.c"


def main():
    if len(sys.argv) > 1:
        log = pathlib.Path(sys.argv[1])
    else:
        log = LOG
    lines = log.read_text(errors="replace").splitlines()
    names = set()
    for line in lines:
        if ".data.obj : error LNK2001:" not in line and ".rodata.obj : error LNK2001:" not in line:
            continue
        match = re.search(r"unresolved external symbol ([A-Za-z_]\w*)", line)
        if match:
            names.add(match.group(1))
    if not names:
        raise SystemExit("No missing data-table symbols were found in the link log")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    body = ["/* Generated UWP equivalents of port/tools/link.py trap stubs. */",
            "#include <windows.h>", "#include <stdlib.h>",
            "static void missing(const char *symbol) {",
            "    OutputDebugStringA(\"bt3: unimplemented game library symbol: \" );",
            "    OutputDebugStringA(symbol);",
            "    ExitProcess(3);",
            "}"]
    for name in sorted(names):
        body.append(f'void {name}(void) {{ missing("{name}"); }}')
    OUT.write_text("\n".join(body) + "\n")
    print(f"Generated {len(names)} game library trap stubs: {OUT}")


if __name__ == "__main__":
    main()
