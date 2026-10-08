# PGMEmu

PGMEmu is an emulator of the IGS PolyGame Master (PGM) arcade board. Its hardware is ported from
the MiSTer FPGA core.

- It runs `.pgm` cartridge images made by PGMBuilder, with the PGM BIOS from `pgm.zip`.
- It has a desktop frontend built on SDL3 and Dear ImGui.
- A headless runner and control servers (JSON-lines and MCP) let scripts and AI agents drive it.

## Building

The build needs CMake 3.25+, Ninja and a C++23 compiler (Apple clang, GCC or MSVC). Every
dependency, SDL3 included, is fetched at a pinned version when the build is first configured, so
that configure needs the network.

```sh
cmake --preset release && cmake --build --preset release
```

`-DPGM_BUILD_APP=OFF` leaves out the desktop application, and with it SDL3 and ImGui.

## Running

Games are `.pgm` images. `scripts/make-pgm.sh` builds them into `roms/` from the MAME sets in
`../ROMS`, using PGMBuilder from `../PGMBuilder`.

```sh
build/release/src/pgm_app/pgmemu --bios ../ROMS/pgm.zip --rom-dir roms orlegend   # play: arrows, Z X C V, 1 start, 5 coin; View > Input to change
build/release/src/pgm_cli/pgmemu-cli --info roms/orlegend.pgm     # describe an image
build/release/src/pgm_cli/pgmemu-cli --server --bios ../ROMS/pgm.zip --rom-dir roms
```

In the desktop application a game can also be opened from File > Open, or by dropping its
`.pgm` onto the window. Started without `--bios`, it takes `pgm.zip` from beside the game, or a
folder up, or from File > Choose BIOS, and remembers it. Holding Backspace, or a gamepad's left
shoulder, rewinds up to 30 seconds; Emulation > Rewind turns that off. Emulation > Run-ahead
shows each frame one to three frames ahead, so that a control is seen sooner. The control
protocol is [docs/spec/control-protocol.md](docs/spec/control-protocol.md).

Every game the MiSTer core supports runs, protection included. The plan is in
[docs/plans/milestones.md](docs/plans/milestones.md), and the design in
[docs/architecture.md](docs/architecture.md).

## Licence

PGMEmu is licensed under GPL-2.0; see [LICENSE](LICENSE). The reason is in
[docs/decisions/0004-licence-gpl-2.md](docs/decisions/0004-licence-gpl-2.md).
