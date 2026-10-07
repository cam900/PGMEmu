# 0010: The ROM cache's timing is not reproduced

**Status:** accepted

## Context

The MiSTer core reads the 68000's ROM from SDRAM through a small cache (`rtl/rom_cache.sv`):
direct-mapped, 256 lines of 8 bytes, emptied on reset. On a miss it freezes the 68000's clock
until SDRAM answers. It then runs the clock at a phase per master tick, 25 MHz instead of 20,
until the lost time is made good. The board has no such cache; it is an artefact of the FPGA.

After 600 frames of the BIOS, one countdown in work RAM was a vblank further on in the emulator
than in the RTL simulation. That was the only difference besides dead stack. The BIOS copies the
Z80's sound driver out of ROM at boot and verifies it, a long run of sequential ROM reads, so the
cache was the first suspect.

## Decision

The cache's timing is not modelled. The emulator reads ROM without delay, as the board does.

The decision was taken after the model was built and measured. It reproduced the cache line for
line, with a fixed miss latency and the catch-up. The emulator reached the end of the BIOS's
boot this many master ticks before the RTL:

| Miss latency (master ticks) | Lead over the RTL |
|---|---|
| none (no model) | 1779 |
| 8 (the simulator's uncontended CPU port) | about 1770 |
| 20 | 1555 |

The catch-up makes good almost all the time a miss loses before the next miss. Even an
overstated latency left most of the lead in place, so the cache is not where it comes from.

## Consequences

- One class and a call in Moira's `sync()` fewer, and no model of an artefact the board does
  not have.
- The lead is still to be explained; [docs/hardware/differences.md](../hardware/differences.md)
  and [docs/open-questions.md](../open-questions.md) carry it, VRAM contention first.
- Should a game's timing ever be shown to depend on the cache, the measurement above is where
  to start again.
