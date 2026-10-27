# Shared brief: fighter action handlers and technique code (sixth batch)

Read docs/agent_rules.md first and follow it exactly, including its last section. Then read
docs/systems/fighter.md, docs/systems/input.md and docs/netplay_notes.md.

Historical disassembly snapshot (not included in this repository):
scratchpad/snap2/asm/cod/*.s
(find a function with `grep -ln "^glabel NAME$"`; data in .../snap2/asm/data/cod/*.s).
The snapshot predates the newest names: a function shown there as `func_XXXXXXXX` may already
have a name in config/symbols/*.txt. Look addresses up there (`grep -rn "0x001D4F30" config/`)
and use the current name in your C.

## What is already known (read these files; they are finished, matching C)

The fighter core was decompiled in the previous batch. Its sources and headers are in
src/battle/ and include/battle/ (some are being linked right now by an integrator; do not edit
them, and declare what you need locally with your own view types as the rules say):

- btl_char_action.c / .h : the action state machine. A fighter is in one of 0x13C actions; the
  handler table `gBtlActHandlers` is at 0x2C4980 (data file in the snapshot: grep for that
  address). A handler is `s32 handler(chr, phase)` with phase 0 enter (once), 1 run (every
  frame, including the enter frame), 2 decide (every frame, before forced actions), 3 leave.
  The return value is ignored; the handlers in the original are int functions with NO return
  statement (as `void` their last call becomes a tail call, which the original does not have).
  `BtlAct_Request(chr, id)` asks for the next action; `BtlAct_SetQueue` / `BtlAct_GetQueued`
  manage the four-slot follow-up queue; `BtlAct_GetCurrent`, `BtlAct_GetIdClass`,
  `BtlAct_PrepareAttack`, `BtlAct_LatchAttack`. Fighter +0x948 is `{current, request, prev,
  queue[4]}`, +0x964 frames in the action, +0x3D0 0x50 bytes of per-action scratch zeroed on
  every switch. The header has a table of action ids with their handler addresses.
- btl_char_status_anim.c : animation. `BtlAnim_Play(chr, anim, blend)`, `BtlAnim_Request`,
  `BtlAnim_Advance(chr, flags)` (returns 1 at the end), `BtlAnim_AdvanceThen`,
  `BtlAnim_AdvanceLoop`, `BtlAnim_TestAttr`, `BtlAnim_PassedFrame` etc. Fighter +0x974 is the
  current animation id.
- btl_char_move.c : movement. `BtlMove_Step`, `BtlMove_TurnYaw`, `BtlMove_SetDirection`,
  `BtlMove_Advance`, `BtlMove_ApplyGravity`, impulses, warps relative to the opponent. The pose
  block at fighter +0x10 is documented in its header and in btl_char_ctl.h.
- btl_char_ctl*.c : `BtlInput_TestAction(chr, id, want)` (114 numbered input conditions, table
  in btl_char_ctl.h), stick readers, placement (`BtlChar_Place*`), the character-change request
  queue (`BtlChange_*`), pose <-> object copies, snapshots.
- btl_char_flag*.c : the flag system (317 flags; plain flags last one frame, held flags until
  cleared; `BtlChar_SetFlag`, `SetHeldFlag`, `ClearFlag`, `TestFlag`, `TestPrevFlag`,
  `IsFlagRaised`), fighter sound requests (`BtlCharSnd_*`), the `BtlOpp_*` opponent helpers,
  the three clash sequences.
- btl_char_hit.c, btl_char_coll*.c : hit detection, guard, reactions (reaction block at
  fighter +0xFB0), throws. btl_char_member.c : members, health/ki/blast gauges
  (`BtlMember_*`), `BtlMember_Damage`. btl_char_status.c : stat modifiers and curves
  (`BtlStat_*`). btl_input.c : the input record and readers (`BtlInput_IsHeld`, ...).
  btl_char_cam*.c : fighter camera (`ChrCam_RequestCut`, ...). btl_char_get.c, btl_char_api.c :
  accessors. btl_char_mgr.c : roster and per-frame phases.

Conventions: fighter object 0x1600 bytes; +Y is down; angles in radians; 30 frames per second;
speeds are per frame. Several source constants were written through macros: speeds as
`BTL_KMH(x) = x * 1000.0f / 3600.0f * (1.0f / 30.0f)` and angles as
`BTL_DEG(x) = x / 180.0f * 3.14159265f` (see btl_char_move.c). Decimal float literals are
truncated by this compiler, so a constant that is one bit off usually means the source used an
expression: try these forms.

## What to deliver

Decompile your whole range in address order. Small functions first; a function that resists
after real effort becomes INCLUDE_ASM with the best attempt in `#if 0` (and is still described
in the report from its disassembly). Handlers are large; getting 70% of a range matched with
the rest documented is a good result.

Naming: every function gets a descriptive name. For an action handler, name it for what the
action IS in game terms when you can tell (from the animations it plays, the input conditions
that lead to it, the flags it sets, the voice kinds, the hit reactions it requests), e.g.
`BtlAct_GuardHandler`; where you cannot tell, name it after its action ids
(`BtlAct_Action47to4C`) and mark it `// guess`. Say in the report which action ids each handler
serves (read the handler table).

Report, in addition to what agent_rules.md asks: for each action or function group, what it
does in game terms and how confident you are; every random draw (which generator:
`BtlChar_Rand`, `BtlChar_FrameMod`, libc `rand`, `Rand_Range`, `Rand_Float*`) and what it
decides; anything that depends on non-simulation state (pad read directly, camera pose, sound
or voice status, loading state, split-screen); anything that depends on which fighter is
player 0; and a field table for every fighter field you touch that is not already documented
in docs/systems/fighter.md.
