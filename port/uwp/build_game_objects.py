#!/usr/bin/env python3
"""Compile game sources to UWP objects."""

import argparse
import concurrent.futures
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
import eeconst  # noqa: E402
import irfix  # noqa: E402
import portsrc  # noqa: E402

TRIPLE = "x86_64-pc-windows-msvc"
OUT = ROOT / "port/build/uwp/game"
BASE = ["clang", f"--target={TRIPLE}", "-std=gnu89", "-fms-extensions",
        "-mno-ms-bitfields", "-msoft-float", "-mno-sse", "-mno-mmx",
        "-fno-strict-aliasing", "-ffp-contract=off", "-fcommon",
        "-fno-stack-protector", "-fno-builtin", "-Wno-everything", "-O2",
        "-DPORT=1", "-DBT3_UWP=1", "-DWINAPI_FAMILY=WINAPI_FAMILY_APP",
        "-Iinclude", "-Iport/include"]


def run(args):
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True)


def compile_one(source):
    import ptr32  # libclang is loaded in each worker only when needed

    prepared, _, missing = portsrc.prepare(source)
    stem = str(source.relative_to(ROOT)).replace("/", "_").replace("\\", "_")[:-2]
    path = OUT / stem
    preprocessed = path.with_suffix(".i")
    llvm_ir = path.with_suffix(".ll")
    obj = path.with_suffix(".obj")
    common = BASE + [f"-I{source.parent}", "-include", "port_libm.h",
                     "-include", "port_compat.h"]
    if source == ROOT / "src/main.c":
        common += ["-Dmain=__real_main", "-DProgress_Main=__wrap_Progress_Main"]
    elif source == ROOT / "src/menu/menu_a_b.c":
        common.append("-DProgress_Main=__real_Progress_Main")
    result = run(common + ["-E", str(prepared), "-o", str(preprocessed)])
    if result.returncode:
        return source.name, "preprocess", result.stderr
    preprocessed.write_text(eeconst.transform(preprocessed.read_text(encoding="latin-1")), encoding="latin-1")
    try:
        rewritten = ptr32.transform(str(preprocessed), ["-x", "cpp-output",
            f"--target={TRIPLE}", "-std=gnu89", "-fms-extensions", "-w",
            "-Wno-error=return-mismatch", "-Wno-error=return-type",
            "-mno-ms-bitfields"])
    except RuntimeError as error:
        return source.name, "ptr32", str(error)
    preprocessed.write_text(rewritten, encoding="latin-1")
    result = run(BASE + ["-S", "-emit-llvm", "-Xclang", "-disable-llvm-passes",
                         "-x", "cpp-output", str(preprocessed), "-o", str(llvm_ir)])
    if result.returncode:
        return source.name, "LLVM IR", result.stderr
    fixed, _ = irfix.fix(llvm_ir.read_text(encoding="utf-8"))
    llvm_ir.write_text(fixed, encoding="utf-8")
    result = run(["llc", "-O2", "-filetype=obj", f"-mtriple={TRIPLE}",
                  "-relocation-model=static", str(llvm_ir), "-o", str(obj)])
    if result.returncode:
        return source.name, "codegen", result.stderr
    return source.name, "ok", f"{len(missing)} source functions have no C body" if missing else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--limit", type=int, help="Compile the first N game sources for a toolchain check")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--only", help="Compile only a source filename, for an incremental rebuild")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    sources = portsrc.sources()
    if args.only:
        sources = [source for source in sources if source.name == args.only]
        if not sources:
            parser.error(f"No game source named {args.only}")
    if args.limit:
        sources = sources[:args.limit]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(compile_one, sources))
    failed = [(name, stage, detail) for name, stage, detail in results if stage != "ok"]
    print(f"UWP game objects: {len(results) - len(failed)}/{len(results)} compiled")
    for name, stage, detail in failed[:30]:
        lines = [line for line in detail.splitlines() if "error:" in line or "Error" in line]
        print(f"  {name} [{stage}]: {(lines[0] if lines else detail[:220])[:220]}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
