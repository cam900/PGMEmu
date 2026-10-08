# 0016: Vertical games are known by their set's name

**Status:** accepted

## Context

CAVE's shooters on the PGM (DoDonPachi II and III, Ketsui, Espgaluda) were built for a monitor
turned on its side. The board draws 448 by 224 for them as for any game, with the picture lying
with its top at the player's left. Nothing in the board, and no field of a `.pgm` header
([spec/pgm-format.md](../spec/pgm-format.md)), says which way the monitor stood. The MiSTer core
leaves it to a switch in its menu, and its MRAs tag the four sets `vertical` for the menu's lists.
MAME draws each of their sets ROT270.

## Decision

- `cart::orientationOf` names a set vertical when it is one of the sets MAME draws ROT270, every
  other horizontal. A header names its set as MAME does.
- `emu.cartridge_info` reports it as `orientation`; frames and screenshots stay as the board draws
  them.
- The application turns a vertical game's picture a quarter anticlockwise, after the shader, so
  that its scanlines and mask run as a turned monitor's did. A setting leaves it lying, for a
  monitor turned by hand.

## Consequences

- A vertical set MAME does not have, or one renamed, shows lying until it is added to the list.
- Should the format gain an orientation field, it would replace the list.
