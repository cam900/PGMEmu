# 0014: A cartridge is its image, as RetroHQ's hardware runs it

**Status:** accepted

## Context

The emulator reads the `.pgm` images PGMBuilder writes ([spec/pgm-format.md](../spec/pgm-format.md)).
The same images run on RetroHQ's hardware, and PGMBuilder's per-game code is maintained by James
Boulton of RetroHQ. Bringing up the IGS027A showed that the images are not the dumps the RTL
loads, decrypted and nothing more:

- **Programs are patched for RetroHQ's hardware.** In ket, espgal and ddp3, every
  `move.b d3,(-6,a0)` into sprite RAM is an `or.b`, because "the external bus cannot handle" the
  byte write. martmast's external ARM ROM has an initialisation's byte accesses to shared RAM
  replaced by no-ops, and its internal ROM the checksum that would catch it.
- **The CAVE games' undumped internal ROM is RetroHQ's recreation**, in its edition of September
  2026 (PGMBuilder `e56bfad`), with the region patchable at 0x20. The MiSTer core's MRAs load the
  edition of June (`ef88f33`).
- **External ARM ROMs are decrypted whole.** The RTL decrypts them in part as it loads them, and
  XORs the rest on every read with a table the ARM's program writes.

[0002](0002-the-fpga-core-is-the-reference.md) makes the RTL the reference for what the hardware
does. It does not say what the cartridge holds. The owner wants the images that work on RetroHQ's
hardware to work here as they are.

## Decision

The image is the cartridge. The emulator runs what it holds and follows PGMBuilder's latest
version, James Boulton's changes included. It does not undo PGMBuilder's patches, and it does not
redo work the image has already done: an external ARM ROM is read without the RTL's XOR.

The RTL stays the reference for the board and the chips. Where a difference from it comes from
the image, it is recorded in [hardware/differences.md](../hardware/differences.md) as one.

## Consequences

- Comparison with the RTL holds for the board and the chips, not for what a patched program does
  differently. A game whose image is patched can differ from the RTL with neither at fault.
- The simulator needs RetroHQ's recreated ROM as a file, `type1_cave_fixed.bin`, to run the CAVE
  games at all. It is in their MRAs, not in `../ROMS`, and is the older edition in any case.
- When PGMBuilder changes what it writes, the regression suite's golden frames change with it, and
  are recorded again after the images are rebuilt.
