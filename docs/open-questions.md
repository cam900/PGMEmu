# Open questions

These questions are deferred on purpose. Each one is removed in the commit whose decision record
answers it. **This file only shrinks.**

## Wait states on the 68k bus

The RTL stalls the 68k on SDRAM cache misses, VRAM contention, IGS022 activity and ARM
shared-RAM misses. The emulator has no caches, so it has to charge wait states as fixed costs
instead. Which costs, and where? Start with none, add VRAM contention, and settle the rest by
comparing with the RTL simulation in M2 and M3.

## Mid-frame sprite state

The sprite engine prescans the whole frame. If a game changes the sprite list or zoom registers
during the frame, the emulator must reproduce exactly when the prescan reads them. Find out from
the RTL and from the games during M3 whether any game does this.

## Where the RTL departs from the board

BG zoom, the zoom table and the IGS022 stall all follow the RTL for now
([0002](decisions/0002-the-fpga-core-is-the-reference.md)). When, and against what evidence,
does the emulator correct them: PGMTest on real hardware, or captures? The answer belongs in
`docs/hardware/` once the first correction is made.
