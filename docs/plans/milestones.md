# Milestones

This file sets the order in which the emulator described in [architecture.md](../architecture.md) is
built. Each entry is one milestone, made of session-sized tasks.

**This file only shrinks.** A milestone is removed in the commit that completes it. A task that
needs a decision the records do not hold stops, and puts the question in
[open-questions.md](../open-questions.md) instead of deciding it.

A session takes work by saying *build M<n> of docs/plans/milestones.md*. Every task ends with:

- the suite green;
- `./scripts/format.sh --check` and `./scripts/tidy.sh` clean;
- documentation updated in the same commit;
- no commit until the owner has reviewed.

Every milestone ends with an **exit** condition that can be checked without a person looking at
a screen. That is why the control API and the headless runner come before most of the hardware.

## M7: Protection

*Delivers,* in order:

1. `Igs027a` type 2: kov2, kov2p, ddp2, martmast, dw2001, dwpc.
2. Type 3: dmnfrnt, theglad, svg, killbldp, happy6.

*Exit:* every game the MiSTer README lists as supported reaches gameplay, and its golden frames
are recorded.

## M8: Finish

*Delivers:*

- Input mapping UI for four players and several gamepads.
- Shader presets and integer scaling.
- Rewind and run-ahead.
- A profiling pass against three times real time on every game.
- A macOS app bundle; CI on macOS and Linux.
