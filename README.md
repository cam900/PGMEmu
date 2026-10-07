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

```sh
build/release/src/pgm_app/pgmemu              # the desktop application
build/release/src/pgm_cli/pgmemu-cli --server # JSON-lines control on stdio
```

The control protocol is [docs/spec/control-protocol.md](docs/spec/control-protocol.md).

PGMEmu cannot run games yet. The plan is in [docs/plans/milestones.md](docs/plans/milestones.md),
and the design in [docs/architecture.md](docs/architecture.md).

## Licence

PGMEmu is licensed under GPL-2.0; see [LICENSE](LICENSE). The reason is in
[docs/decisions/0004-licence-gpl-2.md](docs/decisions/0004-licence-gpl-2.md).
