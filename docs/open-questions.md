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

## When the Z80 gives up its bus

The emulator stops the Z80 for the 68000's bus request before its next opcode fetch, and lets it
go on at once when the bus comes back. tv80s stops at the end of any machine cycle and resumes two
ticks late. Modelling tv80s's way brings PGMTest's `z80_ics_test` and `ics2115_vol_pan` into step
with the RTL simulation, and takes orlegend's attract sound out of step after 18 seconds; the
way kept matches orlegend and the BIOS sample for sample, and leaves those two pages starting
the ICS2115 21 and 29 samples early ([hardware/differences.md](hardware/differences.md)). Neither
way matches all three, so something else differs too: perhaps where z80.h and tv80s place an
instruction's accesses within its machine cycles. The simulator does not expose the Z80's
address bus or /BUSAK; tracing them there, around orlegend's bus requests, would answer it.

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
