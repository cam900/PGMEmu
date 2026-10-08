# 0021: PGMBuilder's code is not carried into the emulator

**Status:** accepted; supersedes the PGMBuilder point of [0004](0004-licence-gpl-2.md)

## Context

[0004](0004-licence-gpl-2.md) said PGMBuilder's code may be reused, as the owner's own. PGMBuilder
is released under GPL-3, which cannot be combined with this project's GPL-2-only code, and part of
it, the per-game data and the protection recreations, is James Boulton's. The owner could relicense
his own part, but not the rest.

## Decision

- No code of PGMBuilder's is carried into the emulator. The emulator reads `.pgm` images from the
  format's description ([spec/pgm-format.md](../spec/pgm-format.md)), written anew, as it does
  today.
- PGMBuilder stays a separate program that makes the images, built and run by
  `scripts/make-pgm.sh` from its own checkout.

## Consequences

- A change to the format is followed by changing the emulator's reader and the format's
  description, never by copying PGMBuilder's `pgm.hpp` or its writer.
