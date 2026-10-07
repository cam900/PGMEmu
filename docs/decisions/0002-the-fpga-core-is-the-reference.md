# 0002: The FPGA core is the hardware reference

**Status:** accepted

## Context

Three descriptions of the PGM board exist in or near this workspace:

- the MiSTer FPGA core (`../Arcade-IGSPGM_MiSTer/rtl`), with a Verilator simulation of it;
- PGMTech (`../PGMTech/README.md`), the owner's own documentation of the board;
- MAME's PGM driver.

An emulator has to pick one of them to settle disagreements, and to port from.

## Decision

The RTL is the reference. Each hardware module of the emulator is a port of an RTL module and
names that module, and the commit of the MiSTer core it was ported from, in its header comment.
PGMTech is the reference for facts the RTL does not encode, such as register intent and the
cartridge pinout. MAME is not a reference.

Where the RTL is known to be incomplete or approximate, the emulator copies the RTL first and
records the gap in `docs/hardware/`. Known gaps at the time of writing:

- BG zoom is stubbed.
- The sprite engine uses a hard-coded `scale_pattern` and ignores the CPU-written zoom table.
- IGS022 completion is modelled by stalling the 68k.

## Consequences

- The emulator can be checked against the RTL mechanically. The Verilator sim runs the same
  ROMs, and [0005](0005-one-control-api.md) makes both speak one protocol, so divergence is found
  by comparing RAM and frames rather than by reading code.
- Games that do not work in the RTL either (kov, kovplus, kov2p, dwpc, happy6, per the MiSTer
  README) have nothing to be checked against. They come last.
- The upstream `.pgm` decryption in PGMBuilder is MAME-derived. That is acceptable: it happens
  before the emulator sees the file, and it decides nothing about how the board behaves.
