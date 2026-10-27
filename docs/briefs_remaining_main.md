# Shared brief: the rest of the main executable (eighth batch)

Read docs/agent_rules.md and docs/decomp_guide.md first and follow them exactly (the guide's
matching lessons are the experience of about 60 agents: read all of it). Then read
docs/README.md and the docs/systems/ file closest to your range (graphics.md, audio.md,
battle.md, boot_and_frame.md, files_and_assets.md, memory.md).

Goal of the project right now: decompile EVERYTHING to matching C, byte for byte. Your range is
code nobody has looked at yet. Nothing is known about it except what its callers say.

Historical disassembly snapshot (not included in this repository):
scratchpad/snap3/asm/cod/*.s
(find a function with `grep -ln "^glabel NAME$"`; data in .../snap3/asm/data/cod/*.s). Names of
everything already decompiled are in config/symbols/*.txt: look an address up there
(`grep -rn "0x0024D610" config/`) and use the current name. Linked sources in src/ call into
your range by `func_XXXXXXXX` placeholders, often with a comment saying what the function
appears to do: `grep -rn "func_0021[0-9A-F]*" src include` for your addresses is the fastest
way to learn what your code is.

Rules of the road:
- Write only the files named in your task (new files). Do not edit any existing file. Do not
  run ninja, configure.py or any git command that changes state. Scratch goes in
  build/scratch_<your stem>/.
- Verify with `python3 scripts/fdiff.py <your file.c>`. fdiff masks call targets and
  gp-relative offsets and cannot see data: ALWAYS compare the bits of the .lit4 / .rodata /
  .sdata your object emits with the original data, and check every call target by address.
- Decompile the whole range in address order. Small functions first. A function that resists
  after real effort becomes INCLUDE_ASM with the best attempt in `#if 0` above it and a note
  of what differs; it must still be described from its disassembly, and the attempt must be
  behaviourally exact. Hand-written assembly (VU0 / VU1 macro code, cop2 instructions, odd
  register use) cannot be C: keep it as a top-level `__asm__` block or INCLUDE_ASM and say so.
- If the range is two or more unrelated modules, split it into several C files at the module
  boundaries (`<stem>.c`, `<stem>_b.c`, ...) and say where and why.
- Every function gets a descriptive name (prefix by module); names that are guesses are marked
  `// guess` in the symbol file. Suggest the proper final file name for each module (your stem
  is only a placeholder).
- The vector / matrix library is named now: config/symbols/vu0_a.txt, vu0_b.txt (e.g.
  `Mtx_Mul(dst, a, b)`, `Vu0Cur_Push / Pop`, `Vec4_Lerp(dst, a, b, t)` = a*t + b*(1-t),
  `Vec4_SetZeroW1`). Game conventions: +Y is down, 30 frames per second, angles in radians.

Report as agent_rules.md asks, plus: what each module is and who calls it; the data structures
(with offsets) and the tables that register your functions; emitted data with original
addresses; every random draw and its generator; anything that reads the pad, the clock, the
camera or the screen mode; file / asset formats your code parses; original bugs; and for the
integrator: object boundaries you found evidence for (a `beqz` / `beqzl` difference that goes
away when a neighbour's function is defined in the same file is such evidence).
