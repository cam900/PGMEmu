# Open questions

These questions are deferred on purpose. Each one is removed in the commit whose decision record
answers it. **This file only shrinks.**

## Timing drift against the RTL

Checkpoints in the BIOS's boot show the emulator running some loops about 0.03 % faster than the
RTL simulation, in code that copies ROM into the Z80's RAM and reads both back
([hardware/differences.md](hardware/differences.md)). Before the interrupt acknowledge's E-clock
wait was modelled, a drift of this size moved an event of orlegend by a frame by frame 1200; no
tested outcome depends on it now, but a longer run or another game may. Two candidates are left:

- the background layer's VRAM reads, a few dots per tile, which the emulator does not charge;
- the RTL's 68000 clock, which stops on every SDRAM access that misses the ROM cache and catches
  up at 25 MHz afterwards. A model of it was measured to matter little
  ([0010](decisions/0010-rom-timing.md)), but only on one stretch of code.

Is exact long-run equality worth the cost, or should comparisons over long runs start from a
shared state instead (M6 starts from RTL save states)? Either answer is a record.

## Which 68000 is right where Moira and SingleStepTests disagree

Moira and the SingleStepTests 68000 suite disagree on shift counts beyond the operand's width,
undefined flags of CHK, DIVS and DIVU, the timing of ADDQ/SUBQ.l to an address register and of
BTST on an immediate, LINK on A7, and every address error (`tests/cpu/M68kSingleStepTest.cpp`).
fx68k, the RTL's 68000, is the reference ([0002](decisions/0002-the-fpga-core-is-the-reference.md)).
Which side does it take? Running the suite's cases on fx68k in the Verilator simulation would
answer it.

## Where the RTL departs from the board

BG zoom, the zoom table and the IGS022 stall all follow the RTL for now
([0002](decisions/0002-the-fpga-core-is-the-reference.md)). When, and against what evidence,
does the emulator correct them: PGMTest on real hardware, or captures? The answer belongs in
`docs/hardware/` once the first correction is made.
