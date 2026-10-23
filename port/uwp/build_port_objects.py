#!/usr/bin/env python3
"""Compile host sources to UWP objects."""

import argparse
import concurrent.futures
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "port/build/uwp/host"
DATA_OUT = ROOT / "port/build/uwp/data"
SDL = ROOT / "build/SDL3-uwp/include"
GEN = ROOT / "port/build/gen/gs"
BASE = ["clang", "--target=x86_64-pc-windows-msvc", "-std=gnu99",
        "-fms-extensions", "-mno-ms-bitfields", "-fno-strict-aliasing",
        "-ffp-contract=off", "-fno-builtin", "-fno-stack-protector",
        "-Wno-everything", "-O2", "-c", "-DBT3_UWP=1",
        "-DWINAPI_FAMILY=WINAPI_FAMILY_APP", "-D_CRT_SECURE_NO_WARNINGS",
        "-Iinclude", "-Iport/include", "-Iport/src", f"-I{SDL}", f"-I{GEN}"]
BASE += [f"-I{ROOT / 'port/uwp/include'}", "-include", "uwp_time.h",
         "-include", "uwp_stdio.h"]


def compile_one(source):
    rel = source.relative_to(ROOT).as_posix()
    obj = OUT / (rel.replace("/", "_")[:-2] + ".obj")
    cmd = BASE + [f"-I{source.parent}"]
    if rel.startswith("port/src/") and not rel.startswith("port/src/gs/") and source.name not in ("plat_libm.c", "plat_fastvec.c"):
        cmd += ["-msoft-float", "-mno-sse", "-mno-mmx", "-DSF_FAST_CALLS",
                "-include", "math.h", "-include", "stdlib.h", "-include", "port_libm.h"]
    if rel.startswith("src/port/"):
        cmd += ["-DREF_VU0_EXTERN_ARITH", "-msoft-float", "-mno-sse", "-mno-mmx",
                "-DSF_FAST_CALLS", "-include", "math.h", "-include", "stdlib.h",
                "-include", "port_libm.h", "-include", "vu0_names.h"]
    if source.name == "stdio.c" and source.parent == ROOT / "port/uwp":
        cmd.append("-DBT3_UWP_STDIO_IMPLEMENTATION")
    completed = subprocess.run(cmd + [str(source), "-o", str(obj)],
                               cwd=ROOT, capture_output=True, text=True)
    if completed.returncode:
        return source.name, completed.stderr
    return source.name, ""


def compile_newlib(source):
    obj = OUT / ("newlib_" + source.stem + ".obj")
    cmd = BASE + ["-msoft-float", "-mno-sse", "-mno-mmx", "-Iport/include",
                  f"-I{source.parent}", "-include", "newlib_shim.h",
                  str(source), "-o", str(obj)]
    completed = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return source.name, completed.stderr if completed.returncode else ""


def compile_cpp(source):
    obj = OUT / ("cxx_" + source.stem + ".obj")
    cmd = ["clang-cl", "/c", "/std:c++17", "/O2", "/MD", "/EHsc-", "/GR-",
           "/DBT3_UWP=1", "/DWINAPI_FAMILY=WINAPI_FAMILY_APP",
           "/D_CRT_SECURE_NO_WARNINGS", f"/I{SDL}",
           f"/I{ROOT / 'port/third_party/imgui'}", f"/I{ROOT / 'port/src/gs'}",
           f"/I{GEN}", f"/Fo{obj}", str(source)]
    completed = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return source.name, completed.stderr if completed.returncode else ""


def compile_data(source):
    obj = DATA_OUT / (source.stem + ".obj")
    completed = subprocess.run(["clang", "--target=x86_64-pc-windows-msvc", "-c",
                                "-x", "assembler", str(source), "-o", str(obj)],
                               cwd=ROOT, capture_output=True, text=True)
    return source.name, completed.stderr if completed.returncode else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    DATA_OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "cxx_imgui_impl_opengl3.obj").unlink(missing_ok=True)
    bars = []
    for name in ("trim", "fill"):
        content = (ROOT / "port/assets/bars" / (name + ".png")).read_bytes()
        bars.append("static const unsigned char kBars" + name.capitalize() +
                    "Png[] = {" + ",".join(map(str, content)) + "};\n")
    GEN.mkdir(parents=True, exist_ok=True)
    (GEN / "bars_png.h").write_text("".join(bars))
    sources = sorted((ROOT / "port/src").glob("*.c")) + \
              sorted(f for f in (ROOT / "port/src/gs").glob("*.c") if f.name != "gs_gl.c") + \
              [ROOT / "src/port/vu0_a.c", ROOT / "src/port/vu0_b.c"]
    generated_stubs = ROOT / "port/build/uwp/gen/stubs.c"
    if generated_stubs.exists():
        sources.append(generated_stubs)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(compile_one, sources))
        results += list(pool.map(compile_newlib, sorted((ROOT / "port/third_party/newlib_libm").glob("*.c"))))
        results += list(pool.map(compile_cpp, sorted(f for f in (ROOT / "port/third_party/imgui").glob("*.cpp")
                                             if f.name != "imgui_impl_opengl3.cpp") +
                                      sorted((ROOT / "port/src/gs").glob("*.cpp"))))
        results += list(pool.map(compile_one, [ROOT / "port/uwp/time.c", ROOT / "port/uwp/stdio.c"]))
        results += list(pool.map(compile_data, sorted((ROOT / "port/data").glob("*.s"))))
        results += list(pool.map(compile_data, [ROOT / "port/uwp/absolute_symbols.s"]))
    failed = [(name, err) for name, err in results if err]
    print(f"UWP port C objects: {len(results) - len(failed)}/{len(results)} compiled")
    for name, detail in failed:
        lines = [line for line in detail.splitlines() if "error:" in line or "fatal error:" in line]
        print(f"  {name}: {(lines[0] if lines else detail[:220])[:220]}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
