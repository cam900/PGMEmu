# PGMEmu: architecture

PGMEmu is a software emulator of the IGS PolyGame Master (PGM) arcade board, written in C++.
It plays `.pgm` cartridge images produced by PGMBuilder in real time, and it is built from the
start to be driven by AI agent sessions as well as by a human with a gamepad.

This document describes the target architecture. The implementation order is in [ROADMAP.md](ROADMAP.md).

## 1. Guiding principles

1. **The FPGA core is the reference.** Hardware behaviour is ported from the MiSTer core in
   `../Arcade-IGSPGM_MiSTer/rtl` (module by module), backed by `../PGMTech/README.md` for documented
   facts. MAME is not a reference. Where the RTL is known to be incomplete or deliberately
   approximate, the emulator copies the RTL behaviour first and records the deviation in
   [hardware-notes.md](hardware-notes.md) (to be created):
   - BG zoom is stubbed in the RTL.
   - The sprite engine uses a hard-coded `scale_pattern` and ignores the CPU-written zoom table.
   - IGS022 completion is modelled by stalling the 68k.
   - IRQ4 fires every 62 lines and sprite DMA runs at line 221.
2. **The core is a library with no platform dependencies.** `pgm_core` has no SDL, ImGui, file
   dialogs or threads. It runs deterministically: the same inputs always produce the same
   frames, audio and RAM. The GUI, the headless server and the tests are thin clients of it.
3. **One control API, several transports.** Everything an agent or a debugger can do goes through
   a single command dispatcher. The GUI debugger, the JSON-lines server, the MCP server and the
   test runner are only different ways of reaching it.
4. **Same protocol as the RTL simulator.** The dispatcher speaks a superset of the JSON-lines
   protocol of the Verilator sim (`../Arcade-IGSPGM_MiSTer/docs/sim-server.md`). One script can
   then drive both, and the emulator can be checked against the RTL frame by frame (differential
   testing, section 8).
5. **Real time with margin.** The target is at least 3x real time on Apple Silicon for every game,
   so that fast-forward, run-ahead and headless test runs are cheap. Accuracy follows the RTL at
   bus and scanline level. Cycle-exactness inside the video chip is not a goal.

## 2. Repository layout

```
PGMEmu/
  CMakeLists.txt, CMakePresets.json
  src/
    core/                 pgm_core: static library, no platform deps
      system/             Machine, Scheduler, Bus68k, clocks, savestate serializer
      cpu/                M68k (Moira adapter), Z80 (chips z80.h adapter), Arm7 (own interpreter)
      video/              Igs023: regs, FgLayer, BgLayer, SpriteEngine, Mixer, Palette
      audio/              Ics2115, Resampler
      io/                 Igs026 (68k<->Z80 latches, Z80 bus control), V3021 RTC, Inputs/DIPs
      cart/               PgmFile (.pgm v0x0021 reader), Bios loader, Cartridge (ROM regions)
      prot/               Asic3, Igs025, Igs022, Igs027a (+ board type 1/2/3 maps)
      debug/              breakpoints, watchpoints, trace ring, disassembler facades
    control/              Command dispatcher, JSON schema of methods, run-until conditions
    app/                  pgmemu: SDL3 + Dear ImGui desktop frontend
      platform/           window, GL renderer, audio out, input/gamepads, config, hotkeys
      ui/                 menus, popups, debugger windows
    server/               transports: JSON-lines (stdio/TCP), MCP (stdio/HTTP)
    tools/                pgmemu-cli (headless batch runner), pgm-info (dump .pgm header)
  third_party/            vendored deps (section 9)
  tests/                  unit, CPU single-step, PGMTest regression, golden frames, diff-vs-RTL
  docs/
```

Executables:

| Binary | Purpose |
|---|---|
| `pgmemu` | GUI emulator. `--server tcp:PORT` or `--mcp-http PORT` lets an agent attach to the session the user is watching. |
| `pgmemu-cli` | Headless. `--server` (JSON-lines on stdio), `--mcp` (MCP on stdio), or batch flags (`--frames N --screenshot f.png --dump wram:f.bin`). |
| `pgmemu-tests` | Unit and regression tests (ctest). |

## 3. Core

### 3.1 Machine and scheduler

The RTL runs everything from one 50 MHz clock with fractional clock enables
(`jtframe_frac_cen`). The emulator uses the same idea, so that timings match the RTL by
construction:

- **Master time base:** 64-bit count of 50 MHz ticks (`MasterTime`).
- **Clock domains as rational dividers of the master clock** (same fractions as `PGM.sv`):

| Domain | Fraction of 50 MHz | Frequency | Clocks |
|---|---|---|---|
| 68k | 2/5 | 20 MHz | Moira cycles |
| Pixel | 1/5 | 10 MHz | IGS023 (640 x 264 dots, about 59.19 Hz) |
| ICS2115 | 615/908 | 33.865 MHz | sample tick = (active_osc + 1) x 32 clocks |
| Z80 | 615/908 / 4 | 8.466 MHz | chips z80 ticks |
| ARM7 | per game | 20 to 33.87 MHz | own interpreter; table from `PGM.sv` |
| RTC | derived from Z80 | 32 kHz | V3021 |

- **Scheduling model: the 68k is the leader, everything else catches up.**
  - The 68k runs in slices up to the next scheduled event: a scanline boundary, an IRQ4 tick,
    vblank start, sprite-DMA line, or the end of the frame.
  - The Z80, ICS2115, ARM7 and video are lazily caught up to the 68k's current time **before**
    any access that can observe or change them. That includes latch read/write, Z80 RAM window
    access, bus request, protection shared RAM and latch, and video register reads such as the
    line counter.
  - At the end of every slice all devices are caught up.
  - The approach is deterministic and cheap. It also gives exact ordering of cross-CPU
    communication, which is what PGM games depend on.
- **Event queue:** a small sorted array of `{MasterTime, EventId}`, covering line start, hblank,
  vblank, IRQ4, sprite DMA, ICS2115 timers and RTC tick. No heap allocations per frame.
- **Main API:** `Machine::RunFrame()` runs until the next vblank start; it is a wrapper around
  `Machine::RunUntil(MasterTime | Condition)`.

### 3.2 CPUs

| CPU | Implementation | License | Why |
|---|---|---|---|
| 68000 | [Moira](https://github.com/dirkwhoffmann/Moira), vendored `Moira/` directory only | MIT | C++20, cycle-exact with correct bus-access timing; it is the vAmiga core. It is a class you subclass: we override `read8/16`, `write8/16`, `sync(cycles)` and the IRQ hooks. `sync()` lets us add wait states (VRAM contention, ROM and protection stalls) the way `PGM.sv` does. It also has a built-in disassembler. Fallback: Musashi (MIT). |
| Z80 | [floooh/chips `z80.h`](https://github.com/floooh/chips) plus `util/z80dasm.h` | zlib | Header-only, cycle-stepped, pin-based (MREQ/IORQ/RD/WR/M1/INT/NMI/BUSRQ). This maps 1:1 onto `igs026_x.sv` and tv80, and handles bus request and NMI timing naturally. |
| ARM7TDMI | Own interpreter (ARMv4T, ARM + Thumb) | ours | No permissively licensed, cleanly embeddable C/C++ core was found. ARMv4T without MMU, cache or coprocessors is a bounded task (about 2 to 3k lines). Validate against the SingleStepTests ARM7TDMI JSON suite. Needed only for IGS027A games (milestone M7). Alternative to evaluate first: SkyEmu's ARM core (MIT). |

Each CPU sits behind a small adapter interface (`ICpuDebug`: registers, PC, step, breakpoints,
disassemble). The debugger and the control API do not depend on the library behind it.

### 3.3 68k bus

`Bus68k` follows `address_translator.sv` and the chip-select mux in `PGM.sv`. Decoding uses a
page table of 64 KB pages over the 24-bit address space (256 entries), each holding either a
direct pointer (ROM, work RAM) or a device handler. Protection boards patch the table per game
(for example IGS022 shared RAM at 0x300000, IGS027A windows at 0x4F0000, 0x500000 and 0xD00000).

Main map (details in PGMTech, "68000 Memory map"):

| Range | Device |
|---|---|
| 0x000000-0x01FFFF | BIOS (128 KB) |
| 0x100000-0x7FFFFF | Cart P-ROM from `cart_prog_base` (0 for BIOS-less CAVE carts) |
| 0x800000-0x81FFFF | Work RAM (128 KB, battery-backed, mirrored) |
| 0x900000-0x907FFF | IGS023 VRAM (wait states) |
| 0xA00000-0xA01FFF | Palette (xRGB555) |
| 0xB00000-0xB0FFFF | IGS023 registers, zoom table, line counter |
| 0xC00000-0xC0000F | IGS026: sound latches, RTC, Z80 control, bus request |
| 0xC04000 | ASIC3 (orlegend) |
| 0xC08000-0xC08007 | Inputs and DIPs |
| 0xC10000-0xC1FFFF | Z80 RAM window (only while the bus is granted) |
| 0xD00000+ | Cart add-ons and protection |

Interrupts: IRQ4 (every 62 lines) and IRQ6 (vblank) are level-triggered and autovectored. They
are cleared by the enable bits in register 0xB0E000, as in `igs023.sv`.

### 3.4 Video: IGS023

Ported from `igs023.sv`, `igs023_bg.sv`, `igs023_fg.sv`, `igs023_sprite.sv` and `igs023_buffer.sv`.

- **Line-based renderer.** At the start of each visible line the renderer produces 448 pixels
  from the register and VRAM state at that moment. This reproduces row scroll and mid-frame
  register changes at line granularity. The RTL also latches per line, so this matches it.
- **Layers:**
  - FG: 8x8 tiles, 4 bpp, transparent 0xF.
  - BG: 32x32 tiles, 5 bpp, row scroll, transparent 0x1F.
  - Sprites: 256 entries DMA'd from work RAM at line 221. The B-ROM bitmask is RLE-like
    against the A-ROM colours; zoom uses the RTL `scale_pattern`; 32 line buffers.
- **Mixer** priority: FG > sprite (prio 0) > BG > sprite (prio 1) > backdrop.
- **Output:** RGBA8888 framebuffer of 448x224, plus optional per-layer debug buffers.
- **Decode caches:** decoded BG/FG tiles and sprite B-ROM line offsets are cached per ROM. They
  are invalidated only on cart load, so tile decoding is not repeated per pixel.

### 3.5 Audio: ICS2115 and output

- `Ics2115` is ported from `rtl/ics2115/*.sv`. `../ICS2115/docs/ics2115_spec.md` and
  `../PGMTest/docs/ics2115*.md` are the secondary references.
- It covers 32 voices with interpolation, volume and pan envelopes, loops, 4-bit/8-bit/u-law and
  noise formats, timers and IRQ to the Z80.
- **Sample-accurate:** one output sample per `(osc + 1) x 32` ICS clocks, which is 33,075 Hz
  with 32 voices.
- The core outputs stereo s16 at the chip's native rate. Frontends resample: the desktop app uses
  a windowed-sinc or polyphase resampler with dynamic rate control to keep audio and video in sync
  at the host's 48 kHz. Tests compare the native-rate output directly with WAVs captured from
  the RTL sim (`audio_capture.*`).

### 3.6 I/O and protection

- `Igs026`: latches 1, 2 and 6 (16-bit from the 68k, low byte from the Z80); NMI on latch 1;
  Z80 reset `0xA659`/`0x5050` (which also resets the ICS2115); bus request `0x45D3`; Z80 I/O map
  (ICS2115 at 0x00xx, latches at 0x01/0x02/0x04xx).
- `V3021`: serial RTC, seeded from host time (or a fixed time in deterministic mode).
- Inputs: 4 players, each with a joystick and 4 buttons, plus coin, start, test and service.
  DIP switches and region come from the `.pgm` region block.
- Protection modules mirror the RTL modules one-to-one:
  - `Asic3` (orlegend)
  - `Igs025` + `Igs022` (killbld, drgw3, dwex)
  - `Igs027a` with board variants:
    - type 1: kovsh, photoy2k, CAVE games
    - type 2: kov2, kov2p, ddp2, martmast, dw2001, dwpc
    - type 3: dmnfrnt, theglad, svg, killbldp, happy6
- `PgmFile.hardware` (AsicClass) selects the board. The per-game ARM clock comes from the
  RTL table.

### 3.7 Cartridge loading

- Input is a `.pgm` v0x0021 file from PGMBuilder:
  - magic `IGSPGM`, big-endian version `00 21`
  - typed 16-byte entries: PRG, INT, EXT, TLE, SPC, SPM, AUD, I22, I25
  - region block
  - The payload is **already decrypted** (P-ROM and ARM EXT), and happy6 is already descrambled.
  - PRG is stored as raw file order (68k words little-endian, low byte at the even address).
- The BIOS (`pgm_p02s.u20`, `pgm_t01s.rom`, `pgm_m01s.rom`) is not inside `.pgm` files. It is
  loaded from a configured `pgm.zip` (miniz) or a directory.
- `pgm-info` prints and validates a `.pgm` header. The format is documented in
  [pgm-format.md](pgm-format.md) (to be written from `PGMBuilder/pgm.hpp`).
- **Note:** the RTL sim's own `.pgm` loader targets the old v0x0010 layout and is wrong for
  current files. Do not copy it; read `PGMBuilder/pgm.hpp` instead.

### 3.8 Save states, NVRAM, rewind

- **Save states:** each component implements `Serialize(StateWriter&)` and
  `Deserialize(StateReader&)` as a tagged chunk (`FOURCC`, version, size). That gives forward
  compatibility and lets tools inspect a single component.
- **Files:** state header + chunks + optional PNG thumbnail. Slots in the GUI; named files through
  the API.
- **NVRAM:** work RAM (128 KB) is saved per game on exit and on request.
- **Rewind and run-ahead:** ring of in-memory states, built on the same serializer.

## 4. Control API: the agent integration

The control API matters most for agent sessions. It is designed as a protocol first; the GUI
debugger windows are built on the same calls.

### 4.1 Dispatcher

`control::Dispatcher` maps `method` plus JSON `params` to a JSON `result`. Methods are grouped
like the RTL sim (names kept compatible where they exist there):

| Group | Methods |
|---|---|
| `emu.*` (alias `sim.*`) | `status`, `load_game{path}`, `reset`, `pause`, `run_frames{count}`, `run_cycles{count}`, `run_until{condition, timeout}` |
| `memory.*` | `read`, `write`, `list_regions` (bios, prog, wram, vram, palette, z80ram, arm_iram, shared, tile, sprite A/B, audio rom), `search`, `dump` |
| `cpu.*` | `get_state{cpu: m68k\|z80\|arm7}`, `set_reg`, `step`, `disassemble{cpu, addr, count}` |
| `debug.*` | `breakpoint.add/remove/list`, `watchpoint.*` (read/write/value), `trace.start/stop/get` (instruction and bus-event ring) |
| `video.*` | `screenshot{path or inline base64}`, `get_regs`, `get_sprites` (decoded sprite list), `render_layer{bg\|fg\|sprites}`, `tile{layer, code}` |
| `audio.*` | `get_state` (ICS2115 voices), `capture.start/stop` (WAV) |
| `input.*` | `set`, `press{button, frames}`, `clear`, `set_dipswitch` |
| `state.*` | `save`, `load`, `list` |
| `nvram.*` | `save`, `load` |
| `test.*` | `status` (PGMTest `.test_status` block at 0x81F000), `debug_link.read/write` (PGMTest RAM FIFO "RFIF") |

Run-until conditions use the sim's grammar: `cpu_pc_equals`, `cpu_pc_in_range`,
`signal_equals`, and the combinators `and`/`or`/`not`. The emulator adds `memory_equals`,
`frame_count` and `line`.

### 4.2 Transports

| Transport | Used by | Notes |
|---|---|---|
| **JSON-lines over stdio** (`pgmemu-cli --server`) | scripts, CI, differential testing, simple agent use | The same wire format as `./sim --server`. Logs go to stderr. |
| **JSON-lines over TCP** (`pgmemu --server tcp:7700`) | attaching to the running GUI | Commands run on the emulation thread between frames. The GUI shows the agent's actions live. |
| **MCP over stdio** (`pgmemu-cli --mcp`) | Claude Code and other MCP clients, headless | Tools are generated from the dispatcher's method table, so they do not drift. Screenshots are returned as MCP image content, which the agent can see directly. |
| **MCP over HTTP** (`pgmemu --mcp-http 7777`) | agent attached to the GUI session | Loopback only by default. |

**Why both MCP and a plain server:**
- MCP gives agents tool discovery, typed parameters and inline images with no glue code.
- The plain JSON-lines server is easier to script, cheaper per call, and identical to the RTL
  sim's protocol, so the same Python harness drives both.

Both are thin adapters over one dispatcher, so supporting both costs little. A project skill
(`.claude/skills/pgmemu`) will document typical agent workflows: boot a game, run to a frame,
inspect sprites, and bisect a divergence against the RTL.

### 4.3 Headless batch mode

```
pgmemu-cli game.pgm --bios ROMS/pgm.zip --frames 1500 \
    --input-script inputs.txt --screenshot out.png --dump wram:wram.bin --hash
```

- Batch mode runs unthrottled; at the target speed that is at least 3x real time.
- The emulator is deterministic: fixed RTC seed, no host time.
- `--hash` prints per-frame CRCs of the framebuffer, work RAM and audio. That makes it the
  building block for golden regression tests.

## 5. Desktop frontend (`pgmemu`)

Gearlynx is the conceptual template (its code is GPL-3, so it is not copied; see section 10).

- **SDL3** (window, events, gamepads, audio stream, native file dialogs). Dear ImGui (docking
  branch) with the SDL3 and OpenGL3 backends. Shader presets come later.
- **Main loop:** poll events, update input, run the emulation, then render ImGui and the game
  texture and present.
  - The emulation runs on its own thread and produces frames into a triple buffer.
  - The audio stream acts as the master clock with dynamic rate control, falling back to vsync
    pacing.
  - Fast-forward, pause, frame step and rewind.
- **Input:**
  - Up to 4 players.
  - Keyboard and SDL gamepads (hot-plug, `gamecontrollerdb.txt`), with per-player binding
    tables in the config.
  - Hotkeys (save/load state, slots, screenshot, fullscreen, fast-forward, pause, reset, test
    and service buttons).
  - ImGui captures input while a debugger window has focus.
- **Config:** INI in the user preferences directory (mINI). Covers recent games, BIOS path, ROM
  folder, video (integer scaling, aspect ratio, filter), audio and input bindings.
- **Debugger windows**, all built on the control API and its data structures:
  - 68k, Z80 and ARM7 disassembly and registers, with breakpoints and stepping.
  - Memory editor (ImGui `imgui_memory_editor`, MIT) over every region.
  - IGS023 registers, BG/FG tilemap viewers, sprite list, sprite inspector and per-layer toggles.
  - Palette viewer.
  - ICS2115 voices.
  - IGS026 latches and Z80 bus state.
  - Protection state.
  - Trace log.
  - PGMTest status.

## 6. Threading

- The core is single-threaded and owned by one emulation thread.
- The GUI thread only reads finished frames and sends commands through a lock-free command queue
  (the same `Dispatcher` requests the network servers use).
- Network servers run their own I/O threads and post requests into that queue.
- Headless mode runs everything on one thread.

## 7. Code conventions

- C++20 (needed by Moira). CMake 3.25+ with presets.
- No exceptions or RTTI in the core. Exceptions are allowed in tools and frontends at the edges.
- Classes are `PascalCase`, methods `PascalCase`, members `m_snake_case`, constants `kName`.
  Hardware register names follow the RTL and PGMTech, so that grep across the repos works.
- Hot paths live in headers (`*_inline.h`). Behaviour is chosen with templates or `if constexpr`,
  not virtual calls, inside per-cycle loops.
- Every hardware module starts with a header comment naming its RTL source file(s) and the commit
  of `Arcade-IGSPGM_MiSTer` it was ported from.
- clang-format, based on `../Arcade-IGSPGM_MiSTer/.clang-format`.

## 8. Testing strategy

1. **CPU single-step tests:**
   - Z80: FUSE tests / zexall.
   - 68000: SingleStepTests/680x0.
   - ARM7: SingleStepTests/ARM7TDMI.

   These validate vendored cores and the own ARM core.
2. **Unit tests:** `.pgm` parser, bus decode table, IGS026 latch semantics, ICS2115 envelope maths
   (against `../ICS2115/docs` tables and `audio_tests/` fits), sprite B-ROM decoder.
3. **PGMTest regression:** the PGMTest BIOS-replacement ROM is run headless, page by page
   (`PGM_PAGE`). The runner reads PASS/FAIL from `.test_status` at 0x81F000 or over the RFIF debug
   link.
4. **Golden frames:** for each supported game, framebuffer CRCs and PNGs at fixed frames with
   scripted input. These catch regressions.
5. **Differential testing against the RTL:**
   - The same JSON script drives `./sim --server` and `pgmemu-cli --server`. A Python harness
     compares work RAM, VRAM, palette, video registers and framebuffers at frame N, then
     bisects to the first diverging frame, line or 68k instruction.
   - This is the main tool for making the emulator match the FPGA core.
   - It is slow on the RTL side (about 1.4 fps), so it runs on demand rather than in CI.
   - RTL save states (`sim/states/`) can seed long-running comparisons.

## 9. Third-party dependencies (all vendored under `third_party/`)

| Library | License | Use |
|---|---|---|
| Moira (`Moira/` subdir only) | MIT | 68000 core and disassembler |
| floooh/chips `z80.h`, `util/z80dasm.h` | zlib | Z80 core and disassembler |
| Dear ImGui (docking) + `imgui_memory_editor` | MIT | UI |
| SDL3 | zlib | system dependency (Homebrew / package) |
| miniz | MIT | BIOS and other zip reading |
| nlohmann/json | MIT | control API and MCP |
| mINI | MIT | config |
| stb_image_write | MIT / public domain | PNG screenshots |
| glad | MIT | GL loader |

## 10. Licensing

- PGMEmu is released under **GPL-2.0**, the same license as the MiSTer core whose logic it ports.
- The MiSTer core ships a GPL-2 license without an "or later" clause. GPL-2-only code cannot be
  combined with GPL-3 code, so code from Gearlynx (GPL-3) is **not copied** into this repository.
  Gearlynx is used as a design reference only.
- PGMBuilder is the project owner's own code, so parts of it (for example the `pgm.hpp` format
  structs) may be reused here. In practice it is used as a tool: it converts MAME zips and raw
  ROM images into `.pgm` files.
- This restriction would go away if the MiSTer core's author confirms "GPL-2.0 or later". In
  that case the project could move to GPL-3 and reuse Gearlynx code directly.
- All vendored libraries are permissive (MIT/zlib) and compatible.
