# PGMEmu: roadmap

The work is ordered so that an agent can verify every milestone headlessly. The control API and
the headless runner come first, before most of the hardware. Architecture:
[ARCHITECTURE.md](ARCHITECTURE.md).

Each milestone ends with **exit criteria** that can be checked automatically.

## M0: Skeleton

- CMake project with presets (`debug`, `release`, `asan`), C++20, vendored `third_party/`.
- Empty `pgm_core` library, `pgmemu-cli`, `pgmemu` (SDL3 window plus ImGui dockspace that shows
  a test pattern), `pgmemu-tests` (ctest).
- `control::Dispatcher` with `emu.status`, and the JSON-lines stdio server.
- `CLAUDE.md` with build and run commands; `.clang-format`.

**Exit:** `cmake --preset release && cmake --build --preset release && ctest` passes;
`echo '{"id":1,"method":"emu.status"}' | pgmemu-cli --server` answers.

## M1: Cartridge and BIOS loading

- `PgmFile` reader for v0x0021 (header, entries, regions, strings). Write
  `docs/pgm-format.md` from `PGMBuilder/pgm.hpp`.
- BIOS loader (zip or directory).
- `pgm-info` tool.
- `memory.read` and `memory.list_regions` over the ROM regions.
- Generate `.pgm` files for the test set with the existing PGMBuilder binary. Keep the generated
  files out of git; a script recreates them from `../ROMS`.

**Exit:** `pgm-info` parses every `.pgm` built from `../ROMS`. ROM region CRCs match the MAME CRCs
listed in PGMBuilder.

## M2: 68000 and system bus; BIOS boots (no video yet)

- Vendor Moira. Write a 68k adapter with `sync()`-based wait states.
- `Bus68k` page table, work RAM, inputs and DIPs.
- Stubs for IGS023 register storage, VRAM, palette and IGS026.
- Scheduler with line, vblank and IRQ4 events; IRQ4 and IRQ6 logic as in `igs023.sv`.
- Control API: `run_frames`, `run_until`, `cpu.get_state`, `cpu.disassemble`, breakpoints.
- Run the 68000 single-step tests on the adapter.

**Exit:**
- With the BIOS loaded, the 68k reaches the same PC/WRAM state as the RTL sim after N frames.
  This is checked with the first version of the differential harness (`tools/diff_vs_rtl.py`)
  on WRAM, VRAM and palette.
- PGMTest `system_basics` reports PASS.

## M3: IGS023 video

- Palette and backdrop, then the FG text layer, then the BG layer with row scroll, then the
  mixer.
- Sprite engine: DMA at line 221, B-ROM mask and A-ROM colour stream, zoom `scale_pattern`,
  32 line buffers, priority.
- `video.screenshot` with inline PNG; layer, tile and sprite viewers in the GUI.
- Real-time GUI playback (keyboard input only at this point).

**Exit:**
- The BIOS logo and the RTC screen match the RTL sim pixel for pixel (framebuffer CRC).
- PGMTest `fg_test`, `bg_test`, `sprite_test` and `video_timing` pass.
- orlegend reaches the intro (frame 1500) and matches the RTL screenshot.

## M4: Sound (Z80, IGS026 and ICS2115)

- chips `z80.h` adapter, Z80 RAM, I/O map, latches, NMI, bus request and reset.
- ICS2115 port: voices, envelopes, timers, IRQ.
- Native-rate audio output, resampler, SDL audio stream with dynamic rate control.
- `audio.capture`, and an ICS2115 voice window in the GUI.
- Z80 single-step tests.

**Exit:**
- PGMTest `z80_ctrl`, `z80_sound_test`, `z80_ics_test` and `ics2115_vol_pan` pass.
- Captured BIOS jingle and orlegend audio match the RTL sim WAV within tolerance (the
  comparison scripts come from `../Arcade-IGSPGM_MiSTer/audio_tests`).

## M5: Agent integration complete

- MCP server: stdio in `pgmemu-cli`, HTTP in `pgmemu`; tools generated from the method table;
  screenshots as image content.
- TCP JSON-lines server attached to the GUI.
- Save states (tagged chunks), NVRAM, `state.*` and `nvram.*`.
- Trace ring (instructions and bus events) and watchpoints.
- `test.status` and the PGMTest RFIF debug link.
- Claude Code project skill: boot, run, inspect, diff vs RTL.

**Exit:** a fresh Claude Code session, given only the skill, can boot orlegend headlessly, step
to a breakpoint, read the sprite list and return a screenshot.

## M6: Regression and differential test suite

- Golden frame CRCs per game, with scripted inputs (coin, start, a few seconds of play).
- `diff_vs_rtl.py` with bisection down to frame, line and instruction; it accepts RTL save
  states as starting points.
- Performance benchmark: frames per second per game in headless mode.

**Exit:** `ctest -L regression` runs all PGMTest pages and golden frames in under 2 minutes.

## M7: Protection

Ordered from simplest to most complex:

1. `Asic3`: orlegend fully playable (region select works).
2. `Igs025` + `Igs022`: killbld, drgw3, dwex.
3. ARM7TDMI core: decide between the own interpreter and the SkyEmu core, then pass the
   SingleStepTests ARM7TDMI suite.
4. `Igs027a` type 1: kovsh and photoy2k, then the CAVE games (ket, espgal, ddp3).
5. Type 2: kov2, kov2p, ddp2, martmast, dw2001, dwpc.
6. Type 3: dmnfrnt, theglad, svg, killbldp, happy6.

**Exit:** every game in the README "Supported games" list of the MiSTer core boots to gameplay,
and its golden frames are recorded.

## M8: Polish

- 4-player input mapping UI and multiple gamepads; hotkeys.
- Shader presets (sharp bilinear, CRT); integer scaling.
- Rewind and run-ahead.
- Profiling pass against the 3x real-time target on all games.
- Packaging: macOS app bundle; CI on macOS and Linux.

## Open questions and risks

- **Line-based rendering vs mid-line effects:** the RTL latches most things per line, but the
  sprite engine prescans the whole frame. If a game changes sprite or zoom state mid-frame,
  emulate the prescan timing exactly.
- **68k wait states:** how VRAM contention and SDRAM cache misses in the RTL translate to fixed
  wait states in the emulator. Decide this by measurement during differential testing (start with
  no waits, then add VRAM waits).
- **Known RTL deviations from hardware:** BG zoom, the zoom table, and the IGS022 stall. Follow
  the RTL first and track the gaps in `docs/hardware-notes.md`. Fix them later against hardware
  captures or PGMTest.
- **Games that do not work in the RTL either** (kov, kovplus, kov2p, dwpc, happy6, per the
  MiSTer README) cannot be checked against the RTL. They wait until the rest is solid.
