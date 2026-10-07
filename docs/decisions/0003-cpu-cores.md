# 0003: CPU cores

**Status:** accepted

## Context

The board has three CPUs: a 68000 at 20 MHz, a Z80 at 8.466 MHz, and, on most later
cartridges, an ARM7TDMI inside the IGS027A protection chip at 20 to 33.87 MHz. The RTL uses fx68k,
tv80 and the Game Bub ARM7TDMI. Those are hardware descriptions and cannot be used here. The
software cores chosen must:

- carry a licence compatible with [0004](0004-licence-gpl-2.md);
- let the emulator insert wait states and observe each bus access, as the RTL's chip-select logic
  does;
- be accurate enough to be compared against the RTL instruction by instruction;
- be fast enough for real time with a wide margin.

## Decision

| CPU | Core | Licence |
|---|---|---|
| 68000 | [Moira](https://github.com/dirkwhoffmann/Moira), the `Moira/` directory only | MIT |
| Z80 | [floooh/chips](https://github.com/floooh/chips) `z80.h`, with `util/z80dasm.h` | zlib |
| ARM7TDMI | Our own ARMv4T interpreter, unless SkyEmu's core (MIT) proves embeddable when M7 starts | ours |

- **Moira** is a C++ class that the emulator subclasses. It is cycle-exact on the 68000, places
  each memory access at its correct cycle, and calls a `sync()` hook through which wait states
  are charged. It is vAmiga's core, and it has a disassembler. Musashi (MIT) is the fallback; it
  is less exact about bus timing and keeps its state in globals.
- **`z80.h`** is cycle-stepped and pin-based: each tick returns MREQ/IORQ/RD/WR/M1 with the
  address and data bus. That maps one-to-one onto `igs026_x.sv`, including bus request and NMI
  timing.
- **ARM7TDMI:** no permissively licensed C or C++ ARM7TDMI core was found that is meant to be
  embedded rather than lifted out of a GBA emulator. ARMv4T without MMU, cache or coprocessors
  is a bounded task, validated by the SingleStepTests ARM7TDMI suite.

## Consequences

- C++20 is the minimum the project can be compiled as, because of Moira; it is compiled as C++23
  ([0007](0007-code-style-is-ngas.md)).
- Each core sits behind an adapter that offers registers, stepping, breakpoints and disassembly.
  The debugger and the control API depend only on the adapter.
- Every core is validated in `tests/` against a published single-step suite before a machine is
  built on it: SingleStepTests 680x0, FUSE/zexall for the Z80, and SingleStepTests ARM7TDMI.
- Ticking the Z80 per clock costs about 8.5 million calls per emulated second, which is well
  within budget.
