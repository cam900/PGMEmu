# Open questions

These questions are deferred on purpose. Each one is removed in the commit whose decision record
answers it. **This file only shrinks.**

## VRAM contention

A 68000 cycle to VRAM waits for `igs023.sv`'s byte-wide state machine. The emulator charges that
as a fixed number of wait states. In the RTL the same cycle also waits while the text and
background layers fetch from VRAM, which they do in windows of every line. When the layers are
emulated (M3), is their fetch schedule modelled per line, so that a VRAM cycle waits exactly as
long as the RTL's does? Or is a per-line average enough? The answer is measured against the
RTL simulation: the BIOS's boot finishes about 35 µs sooner in the emulator today
([hardware/differences.md](hardware/differences.md)).

## Which 68000 is right where Moira and SingleStepTests disagree

Moira and the SingleStepTests 68000 suite disagree on shift counts beyond the operand's width,
undefined flags of CHK, DIVS and DIVU, the timing of ADDQ/SUBQ.l to an address register and of
BTST on an immediate, LINK on A7, and every address error (`tests/cpu/M68kSingleStepTest.cpp`).
fx68k, the RTL's 68000, is the reference ([0002](decisions/0002-the-fpga-core-is-the-reference.md)).
Which side does it take? Running the suite's cases on fx68k in the Verilator simulation would
answer it.

## Mid-frame sprite state

The sprite engine prescans the whole frame. If a game changes the sprite list or zoom registers
during the frame, the emulator must reproduce exactly when the prescan reads them. Find out from
the RTL and from the games during M3 whether any game does this.

## Where the RTL departs from the board

BG zoom, the zoom table and the IGS022 stall all follow the RTL for now
([0002](decisions/0002-the-fpga-core-is-the-reference.md)). When, and against what evidence,
does the emulator correct them: PGMTest on real hardware, or captures? The answer belongs in
`docs/hardware/` once the first correction is made.
