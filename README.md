# PGMEmu

A software emulator of the IGS PolyGame Master (PGM) arcade board, in C++20.

- Hardware behaviour is ported from the MiSTer FPGA core (`../Arcade-IGSPGM_MiSTer`).
- Games are loaded as `.pgm` cartridge images produced by PGMBuilder; the PGM BIOS is loaded from `pgm.zip`.
- Desktop frontend: SDL3 + Dear ImGui. Headless runner and JSON-lines / MCP control servers for agents and tests.

Status: design phase. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/ROADMAP.md](docs/ROADMAP.md).

License: GPL-2.0 (see [LICENSE](LICENSE) and ARCHITECTURE.md, section 10).
