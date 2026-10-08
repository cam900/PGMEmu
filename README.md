# PGMEmu

PGMEmu is an emulator of the IGS PolyGame Master (PGM) arcade board. Its hardware was ported, by
an LLM, from the MiSTer FPGA core of **Martin Donlon (Wickerwaka)**,
[Arcade-IGSPGM_MiSTer](https://github.com/MiSTer-devel/Arcade-IGSPGM_MiSTer). All credit for
understanding the board goes to him.

- Every game the MiSTer core supported in June 2026, which it was ported from, runs, protection
  included: the IGS027A's ARM7 of the later games, the IGS022/IGS025 of The Killing Blade and
  Dragon World 3, and Oriental Legend's ASIC3.
- It runs `.pgm` cartridge images made by [PGMBuilder](https://github.com/laoo/PGMBuilder), the
  format RetroHQ's hardware runs, with the PGM BIOS from `pgm.zip`.
- The desktop application, on SDL3 and Dear ImGui, has shaders (sharp, scanlines, CRT), vertical
  games shown upright, input mapping for four players and their gamepads, rewind and run-ahead.
- A headless runner and control servers (JSON-lines and MCP) let scripts and AI agents drive it.

## Games and the BIOS

**No game, BIOS or ROM of any kind comes with PGMEmu.** You need dumps you own, as MAME's sets.
[PGMBuilder](https://github.com/laoo/PGMBuilder) turns a set's zip into a `.pgm` image; the BIOS
is MAME's `pgm.zip` as it is.

IGS, PolyGame Master and the games' titles are their owners' trademarks. PGMEmu is not affiliated
with them.

## Running

```sh
pgmemu                                      # then File > Open a .pgm, or drop one on the window
pgmemu --bios path/to/pgm.zip --rom-dir path/to/images orlegend
pgmemu-cli --info orlegend.pgm              # describe an image
pgmemu-cli --server --bios pgm.zip --rom-dir images      # JSON-lines control on stdin and stdout
```

Started without `--bios`, the application takes `pgm.zip` from beside the game, or a folder up,
or from File > Choose BIOS, and remembers it. Player 1 is on the arrows, Z X C V, 1 to start and
5 for a coin, and View > Input changes that. Holding Backspace, or a gamepad's left shoulder,
rewinds up to 30 seconds; Emulation > Run-ahead shows each frame one to three frames ahead, so
that a control is seen sooner. The control protocol is
[docs/spec/control-protocol.md](docs/spec/control-protocol.md).

The [releases](../../releases) have builds for macOS (Apple silicon), Linux (x64) and Windows (x64).
The macOS application is not notarized: the first time, open it with right-click > Open, or run
`xattr -dr com.apple.quarantine PGMEmu.app`.

## Building

The build needs CMake 3.25+, Ninja and a C++23 compiler: Apple clang, GCC 14 or MSVC. Every
dependency, SDL3 included, is fetched at a pinned version when the build is first configured, so
configure needs the network.

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release
```

On Linux, SDL3 needs the development packages of the display and sound systems; on Ubuntu:

```sh
sudo apt install ninja-build libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev \
  libegl1-mesa-dev libdbus-1-dev libudev-dev
```

`-DPGM_BUILD_APP=OFF` leaves out the desktop application, and with it SDL3 and ImGui. On Windows
the `windows` preset builds with Visual Studio. The tests that need ROMs, and the regression
suite's golden frames, skip without them; [CLAUDE.md](CLAUDE.md) says where they are looked for.

The design is in [docs/architecture.md](docs/architecture.md), the reasons in
[docs/decisions/](docs/decisions/), and [docs/README.md](docs/README.md) is the index.

## Licence

PGMEmu is released under the GNU General Public License, version 2 only; see [LICENSE](LICENSE),
and [docs/decisions/0004-licence-gpl-2.md](docs/decisions/0004-licence-gpl-2.md) for why.
[THIRD_PARTY.md](THIRD_PARTY.md) names the work it is derived from and the libraries it is built
with, and carries their terms.
