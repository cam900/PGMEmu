# 0017: Vertical games are known by their image's flag

**Status:** accepted; supersedes [0016](0016-vertical-games-by-set-name.md)

## Context

[0016](0016-vertical-games-by-set-name.md) named the vertical sets in the emulator, because no
`.pgm` header said which way a game's monitor stood. The owner has since given the format a
`flags` field: PGMBuilder's version 0x0022 sets its bit 0 for every set MAME draws ROT270, and
for `--vertical` when an image is built by hand
([spec/pgm-format.md §2.2](../spec/pgm-format.md#22-flags)).

## Decision

- The orientation is the image's: `PgmImage::orientation()` reads the flag, and the list of sets
  is gone.
- Only version 0x0022 is read, as only one version has been read before. Images of 0x0021 are
  rebuilt, as those of 0x0020 were.
- Flags the reader does not know are ignored, not refused.
- `emu.cartridge_info` reports it, and frames and screenshots stay as the board draws them, as
  0016 decided.
- The application shows a game as its flag says, turning a vertical one upright after the
  shader. Display > Vertical turns any game either way until the next is loaded, for a monitor
  turned by hand or a horizontal game played on its side; it is not kept, so that each game
  starts as its cabinet stood.

## Consequences

- A game built by hand shows upright when it is built with `--vertical`, and a renamed set keeps
  its orientation.
- Every image built before PGMBuilder `26ebacd` is refused until it is rebuilt.
