# Architecture

This document covers what the components are, how emulated time and data flow through them,
and why the boundaries sit where they do. The reasoning behind individual choices is in
[the decision records](decisions/). Facts about the board itself are in PGMTech
(`../PGMTech/README.md`) and the RTL (`../Arcade-IGSPGM_MiSTer/rtl`), and are not repeated here
([0002](decisions/0002-the-fpga-core-is-the-reference.md)).

## The shape

```
                 ┌──────────────────────── pgm_core (library) ────────────────────────┐
  .pgm + BIOS ──▶│ Cartridge ──▶ Machine ◀── Scheduler                                │
                 │               │  ├─ M68k (Moira)    ├─ Igs023 video  ├─ Igs026 I/O │
                 │               │  ├─ Z80 (z80.h)     ├─ Ics2115 audio ├─ V3021 RTC  │
                 │               │  └─ Arm7 (ours)     └─ protection: Asic3, Igs025,  │
                 │               │                       Igs022, Igs027a              │
                 │               ▼                                                    │
                 │         control::Dispatcher  (every capability, JSON in and out)   │
                 └───────────────▲──────────────────▲─────────────────▲──────────────┘
                                 │                  │                 │
                     pgm_server transports     pgmemu (GUI)      pgmemu-cli (headless)
                  JSON-lines stdio/TCP, MCP    SDL3 + ImGui      batch runs, --server, --mcp
```

The core is a library with no SDL, no ImGui, no threads and no file dialogs, and it is
deterministic: the same `.pgm`, BIOS, inputs and RTC seed always give the same frames, audio and
RAM. Everything else is a client of it. The boundary is drawn for three reasons:

- **Testability.** The suite links the core and nothing else, so every hardware module is tested
  headless.
- **Determinism.** Determinism is what makes golden frames and comparison against the RTL
  possible.
- **Agent use.** An agent session reaches the same machine the GUI shows, through the same
  dispatcher.

## Emulated time

The RTL clocks the whole board from one 50 MHz clock through fractional clock enables. The
emulator keeps the same arrangement. Time is a 64-bit count of 10 ns units, half a master tick:
the largest unit in which both the 68000's and the pixel clock's periods are whole (5 and 10
units). Each clock domain is the RTL's own ratio of the master clock:

| Domain | Ratio of 50 MHz | Frequency |
|---|---|---|
| 68000 | 2/5 | 20 MHz |
| Pixel (IGS023) | 1/5 | 10 MHz |
| ICS2115 | 615/908 | 33.865 MHz |
| Z80 | 615/908 / 4 | 8.466 MHz |
| ARM7 | per game, from `PGM.sv` | 20 to 33.87 MHz |

Because the ratios are the RTL's, a timing difference against the simulation is a bug in the
emulator, not rounding. The clocks with ratios that do not divide evenly are counted in closed
form from master ticks, as `jtframe_frac_cen` counts them, so a device asks how many pulses of
its clock have passed instead of being stepped through each one.

**The 68k leads and everything else catches up.**

1. The 68k runs an instruction at a time. Before each one the raster is brought up to the
   present, so the interrupt lines it sees are those of that moment. Moira calls back before
   every bus cycle, so a cycle reaches its device at the time the 68000 makes it. A 68000
   stopped by STOP skips straight to the next raster event, which is the only thing that can
   wake it.
2. The Z80, the ICS2115, the ARM7 and the video are brought up to the 68k's present before any
   access that could observe or change them. That covers a latch read or write, the Z80 RAM
   window, a bus request, protection shared RAM, and a video register read such as the line
   counter.
3. At the end of every slice, all of them are brought up to the present.

The alternative, interleaving every chip in fixed small quanta, costs more and is still less
exact. What PGM games depend on is the *order* of cross-CPU communication, and catch-up on
access gives that order exactly.

## The core's modules

Each hardware module ports one RTL module, and the module header names its source:

| Module | RTL |
|---|---|
| `Bus68k` | `address_translator.sv`, chip-select mux in `PGM.sv` |
| `Igs023` | `igs023.sv`, `igs023_bg.sv`, `igs023_fg.sv`, `igs023_sprite.sv`, `igs023_buffer.sv` |
| `Ics2115` | `ics2115/*.sv` |
| `Igs026` | `igs026_x.sv` |
| `V3021` | `v3021.sv` |
| `Asic3` | `pgm_asic3.sv` |
| `Igs025` | `igs025.sv` |
| `Igs022` | `igs022.sv` |
| `Igs027a` | `igs027a.sv` |

How some of these modules work, and why:

- **`Bus68k`.** A page table of 64 KB pages over the 24-bit space. Each page holds either a
  direct pointer (ROM, work RAM) or a device. Protection boards patch the table per game.
  Wait states are charged through Moira's `sync()` hook
  ([0003](decisions/0003-cpu-cores.md)).
- **`Igs023`: video is drawn a line at a time, sprites a frame at a time**
  ([0011](decisions/0011-video-is-drawn-by-line-and-by-frame.md)). A line is drawn when the RTL
  starts fetching it; the sprite layer is drawn from the list the DMA copies at line 221, and
  shown from the next vertical blank. The sprite engine (`SpriteEngine`) ports the RTL's prescan
  and row drawing state by state, its zoom patterns and its quirks included.
- **`Ics2115`: audio is produced at the chip's own rate.** That is one stereo sample every
  `(oscillators + 1) × 32` chip clocks. Resampling to the host rate is the frontend's job, so
  the suite can compare the native stream with WAVs captured from the RTL simulation.
- **`Igs027a` comes in board variants.** Types 1, 2 and 3 differ in their memory map and latch.
  The variant and the ARM clock follow from the `.pgm` header's hardware class and the RTL's
  per-game table.

## The cartridge

The core reads the `.pgm` file PGMBuilder writes ([spec/pgm-format.md](spec/pgm-format.md)), and a
BIOS from `pgm.zip` or a directory.
The two halves are separate because PGMBuilder leaves the BIOS out of every image.

PGMBuilder has already decrypted the 68k program and the ARM external ROM, and descrambled
happy6, so none of that exists in the emulator.

The RTL simulator's own `.pgm` loader targets version 0x0010 and is not a reference.

## The control API

Every capability is a method of `control::Dispatcher`
([0005](decisions/0005-one-control-api.md)). The method groups follow the RTL simulator:

- `emu.*` (alias `sim.*`): load, reset, run frames or cycles, run until a condition
- `memory.*`
- `cpu.*`: for `m68k`, `z80` and `arm7`
- `debug.*`: breakpoints, watchpoints, trace
- `video.*`: screenshot, registers, decoded sprites, layers, tiles
- `audio.*`
- `input.*`
- `state.*`, `nvram.*`
- `test.*`: the PGMTest result block and its RFIF debug link

The transports are thin, and none adds a capability of its own:

- JSON-lines on stdio (`pgmemu-cli --server`) and TCP (`pgmemu --server tcp:PORT`)
- MCP on stdio (`pgmemu-cli --mcp`) and HTTP (`pgmemu --mcp-http PORT`)

A request from a GUI-attached transport is executed on the emulation thread between two slices,
so the person at the GUI watches what the agent does.

`pgmemu-cli` also runs batch jobs: run a game for N frames with scripted input, then write a
screenshot, dumps and per-frame hashes. Batch jobs run unthrottled. They are the building block
of the golden-frame tests and of comparison against the RTL.

## The desktop frontend

`pgmemu` is SDL3 with Dear ImGui (docking). Gearlynx is the conceptual model for the event loop,
gamepad handling and debugger layout; none of its code is used
([0004](decisions/0004-licence-gpl-2.md)).

**Threads:**

- The core runs on its own emulation thread and publishes finished frames into a triple buffer.
- The GUI thread never touches the core. It reads frames and sends dispatcher requests through
  a queue, the same path the network transports use.
- Headless mode has one thread.

**Pacing:** the audio stream is the clock, with dynamic rate control between the core's native
rate and the host's. Vsync is the fallback when audio is off. Fast-forward, pause, frame step
and rewind sit on top of it.

**Input:**

- Four players, from the keyboard and from SDL gamepads with hot-plug.
- Per-player bindings, plus hotkeys.
- ImGui captures input while a debugger window has focus.

**Debugger windows:**

- disassembly and registers for each CPU
- memory editor
- IGS023 registers, tilemaps, sprite list and layer toggles
- palette
- ICS2115 voices
- IGS026 latches
- protection state
- trace
- PGMTest status

## Save states

Each module writes a tagged chunk (`FOURCC`, version, size). A state is a header, the chunks, and
an optional thumbnail. Tagged chunks let a module's layout change without invalidating other
modules' data, and let a tool read one module out of a state. Rewind and run-ahead keep a ring of
states in memory, built on the same serializer. Work RAM is battery-backed on the board and is
saved per game as NVRAM.

## Testing

Five kinds of evidence, each catching what the others cannot:

1. **CPU single-step suites:** SingleStepTests 680x0, FUSE/zexall, SingleStepTests ARM7TDMI.
   They hold the cores to the ISA.
2. **Unit tests:** the `.pgm` reader, bus decode, latch semantics, envelope arithmetic and the
   sprite mask decoder. They hold each module to its RTL source.
3. **PGMTest pages** run headless. Each page reports through its result block at WRAM 0x81F000
   or through the RFIF debug link. They hold the machine to hardware-derived expectations.
4. **Golden frames:** per-game framebuffer hashes at fixed frames under scripted input. They
   catch regressions.
5. **Comparison against the RTL simulation:** the same script drives both over the one protocol,
   compares memory and frames, and bisects to the first divergence. It is the main instrument for
   matching the core. The simulation runs at about 1.4 frames per second, so this runs on demand,
   not in the suite.
