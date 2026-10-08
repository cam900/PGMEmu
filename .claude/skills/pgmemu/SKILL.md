---
name: pgmemu
description: Boot, run, inspect and debug games on PGMEmu, the IGS PGM arcade emulator in this repository, and compare it with the MiSTer core's RTL simulation. Use it to load a game or the BIOS headless, run frames, stop at breakpoints or watchpoints, read memory, registers, the sprite list or the sound voices, take screenshots, save and load states, drive PGMTest pages, or check the emulator against the RTL.
---

# Driving PGMEmu

PGMEmu answers a JSON control protocol (`docs/spec/control-protocol.md`, every method with its
parameters). Run it headless as a server that keeps its state, and send it one request per
command with `scripts/pgmemu.py`. Work from the repository root.

## Start it

Build once, then start the server in the background (Bash with `run_in_background`):

```sh
cmake --build --preset release
build/release/src/pgm_cli/pgmemu-cli --server tcp:7701 --bios ../ROMS/pgm.zip --rom-dir roms --state-dir build
```

Games are `roms/<set>.pgm` (build them with `scripts/make-pgm.sh` if `roms/` is empty); `pgm` is
the BIOS alone. When done, stop the one you started, by its port:
`pkill -f "pgmemu-cli --server tcp:7701"`.

If 7701 is taken, pick another port and pass it to every request as `--port PORT`, written out on
each call (zsh does not split a command kept in a variable).

A person may have the desktop application open instead, started with `--server tcp:7701`: then
the same requests drive the game on their screen, and you must not start a second server.

To have the emulator as MCP tools in your own sessions instead, with screenshots shown as
images, register it for yourself (it is not committed, as each session would then run the binary
of its start, and rebuilds are frequent here):
`claude mcp add --scope local pgmemu -- "$PWD/build/release/src/pgm_cli/pgmemu-cli" --mcp --bios "$PWD/../ROMS/pgm.zip" --rom-dir "$PWD/roms"`.
Reconnect it (`/mcp`) after a rebuild.

## Send requests

```sh
scripts/pgmemu.py emu.load_game name=orlegend
scripts/pgmemu.py emu.run_frames count=600
scripts/pgmemu.py video.screenshot path=build/shot.png     # then Read build/shot.png to see it
```

Parameters are `key=value` (JSON values; `0x...` is hex) or one JSON object. The answer is the
result as JSON; an error goes to stderr with exit status 1. Numbers in answers are decimal:
`"pc": 1050780` is 0x10089c.

## What to ask for

| To | Methods |
|---|---|
| Load, reset, see where it is | `emu.load_game {name, region}`, `emu.reset {cycles: 100}`, `emu.status`, `emu.cartridge_info` |
| Region | `emu.set_region {region}`: a code from `emu.cartridge_info`'s `region_info`, such as `JAPN`; powers up anew |
| Run | `emu.run_frames {count}` (60 is a second), `emu.run_cycles {count}` (50 MHz master ticks), `emu.run_until {condition, timeout_cycles}` |
| Stop at code | `debug.breakpoint.add/remove/list {address}`: a run stops before that instruction |
| Stop at data | `debug.watchpoint.add {address, size, access: read/write/access}`: a run stops after the instruction, and its answer says what was accessed and from where |
| See what ran | `debug.trace {count}`: the last instructions, with disassembly |
| CPUs | `cpu.get_state` (68000), `cpu.get_state {cpu: z80}`, `cpu.disassemble {address, count}` |
| Memory | `memory.list_regions`, `memory.read {region, address, size}` (WORK_RAM is 0x800000-0x81FFFF, in 68000 byte order) |
| Picture | `video.screenshot {path}`, `video.sprites`, `video.registers`, `video.layers {text, background, sprites}`, `video.tiles {layer, first, count, palette, path}`, `video.tilemap {layer, path}` |
| Sound | `audio.voices`, `audio.capture_start {path}` / `audio.capture_stop` (a WAV) |
| Input | `input.press {name}` (up, down, left, right, button1-4, start, coin: held two frames, released two), `input.set` / `input.clear` |
| State | `state.save {filename}`, `state.load {filename}`, `state.list`, `nvram.save/load` |
| PGMTest | `test.status` (a page's status block), `debug_link.start/write/read/stop` |

A run answers `reason`: `completed`, `breakpoint`, `watchpoint`, `condition_met`, `timeout` or
`halted`. After a breakpoint, the next run executes that instruction before stopping again; a
breakpoint in code that runs every frame stops every run within a frame, so remove it
(`debug.breakpoint.remove`) to run on.

`video.sprites` answers `{count, sprites}` in list order, a later sprite drawn over an earlier
one. `x` (11 bits) and `y` (10 bits) are raw and wrap: x 1508 is off screen. `width` is in units of
16 pixels, `height` in lines, `scale_x` and `scale_y` are 16 for actual size, and `mask_address`
is a word address in the B ROM.

### Finding an address to stop at

Every game runs its vertical-blank handler each frame, and the BIOS finds it through work RAM:
its level-6 vector leads to `move.l $801478,-(A7); rts`. So, once a game runs, the long at
WORK_RAM 0x1478 (`memory.read region=WORK_RAM address=0x1478 size=4`; work RAM reads in 68000
order) is the game's handler, a good first breakpoint. Or run a few frames and read
`debug.trace` to see the code that is running, then `cpu.disassemble` around it.

The ROM regions (`BIOS_PROG_ROM`, `CART_PROG_ROM`) read in their files' order, each 16-bit word's
low byte first: `0000be0c` there is the long 0x00000cbe.

### Example: stop in a game and look at it

```sh
scripts/pgmemu.py emu.load_game name=orlegend
scripts/pgmemu.py emu.run_frames count=2400            # past the warning screen, into the attract mode
scripts/pgmemu.py memory.read region=WORK_RAM address=0x1478 size=4   # the vblank handler: 0010089c
scripts/pgmemu.py debug.breakpoint.add address=0x10089c
scripts/pgmemu.py emu.run_frames count=60              # reason: breakpoint
scripts/pgmemu.py cpu.get_state
scripts/pgmemu.py video.sprites
scripts/pgmemu.py video.screenshot path=build/orlegend.png
scripts/pgmemu.py debug.breakpoint.remove address=0x10089c
```

orlegend shows a warning screen for its first 1000 frames or so; its attract mode is running by
frame 2400.

## Comparing with the RTL

The reference is the MiSTer core's Verilator simulation in `../Arcade-IGSPGM_MiSTer/sim`, which
speaks the same protocol. `scripts/compare-with-rtl.py` drives both and reports where memory,
pictures and sound differ:

```sh
scripts/compare-with-rtl.py --game orlegend --frames 300 --pictures
scripts/compare-with-rtl.py --frames 800 --regions AUDIO_RAM --audio
scripts/compare-with-rtl.py --program build/tools/pgmtest-<page>/pgm/pgm_p02s.u20 --frames 120 --press down@60
```

The simulation runs at about 1.4 frames a second: 600 frames take seven minutes, so start it in
the background. Bytes below a stack pointer (dead stack) differ wherever an interrupt arrived at
another instruction; leave them out with `--ignore WORK_RAM:1f000-20000`.
`docs/hardware/differences.md` lists what is known to differ, and why.

## Things that trip people up

- Time is counted in master ticks of 50 MHz. A frame is 844,800 of them.
- `video.screenshot` without `path` returns base64; give a path and Read the file to see it.
- A save state belongs to the build and the game that wrote it.
- Requests are answered one at a time; a long `emu.run_frames` holds the others back.
