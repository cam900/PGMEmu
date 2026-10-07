# PGMEmu

PGMEmu is an emulator of the IGS PolyGame Master (PGM) arcade board. Its hardware is ported from
the MiSTer FPGA core.

- It runs `.pgm` cartridge images made by PGMBuilder, with the PGM BIOS from `pgm.zip`.
- It has a desktop frontend built on SDL3 and Dear ImGui.
- A headless runner and control servers (JSON-lines and MCP) let scripts and AI agents drive it.

## Building

The build needs CMake 3.25+, Ninja, a C++23 compiler (Apple clang, GCC or MSVC) and SDL3.

```sh
cmake --preset release && cmake --build --preset release
```

## Running

PGMEmu cannot run games yet. The plan is in [docs/plans/milestones.md](docs/plans/milestones.md),
and the design in [docs/architecture.md](docs/architecture.md).

## Licence

PGMEmu is licensed under GPL-2.0; see [LICENSE](LICENSE). The reason is in
[docs/decisions/0004-licence-gpl-2.md](docs/decisions/0004-licence-gpl-2.md).
