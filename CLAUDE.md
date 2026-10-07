# PGMEmu: notes for agent sessions

IGS PGM arcade emulator in C++20. Read docs/ARCHITECTURE.md before changing structure; docs/ROADMAP.md says what is next.

## Workspace neighbours (read-only references)
- `../Arcade-IGSPGM_MiSTer/rtl` is the hardware reference: port behaviour from here, not from MAME.
  The Verilator sim is in `../Arcade-IGSPGM_MiSTer/sim`. Run it with `PGM_ROM_DIR=../../ROMS ./sim <game>`,
  or headless with `./sim --server` (protocol: `../Arcade-IGSPGM_MiSTer/docs/sim-server.md`). It runs at about 1.4 fps.
- `../PGMTech/README.md`: hardware documentation (memory maps, registers, video, ICS2115).
- `../PGMBuilder`: `.pgm` format (`pgm.hpp`) and converter (`out/build/native/PGMBuilder game.zip outdir`).
  The RTL sim's own `.pgm` loader is outdated (v0x0010); do not copy it.
  PGMBuilder is the project owner's code, so its code (e.g. `pgm.hpp` structs) may be reused. Mostly it is used as a tool.
- `../PGMTest`: test ROM (BIOS replacement). Results are in WRAM 0x81F000 (`.test_status`) and the RFIF debug link.
- `../ICS2115/docs`: ICS2115 spec. `../ROMS`: MAME zips, including `pgm.zip` (BIOS).
- `../Gearlynx`: design reference only. GPL-3, so do not copy code into this repo (GPL-2).

## Rules
- The core (`src/core`) has no SDL, ImGui, threads or file dialogs, and must stay deterministic.
- Every hardware feature is reachable through `control::Dispatcher`. GUI windows and servers are clients of it.
- Each ported module names its RTL source file in its header comment.
- Never commit ROMs or `.pgm` files.
