# 0019: Run-ahead is one setting for every game, off unless chosen

**Status:** accepted

## Context

A PGM game answers a control a frame or more after it is pressed: it reads the controls in one
frame, moves in the next, and the video chip draws its sprites a frame after that. Run-ahead hides
those frames. After each frame run it saves the state, runs N frames more with the same controls
held, shows the last of them, and loads the state back, so that what is shown is N frames ahead
of what is kept. It hides only frames in which the picture does not answer yet; past them, it
skips the pictures a game shows on purpose, such as a fighter's first steps.

`scripts/measure-lag.py` finds the frame on which each game's picture first answers a control,
from a point in play. Measured on 2026-10-08:

| Answers on frame | Run-ahead that hides the rest | Games |
|---|---|---|
| 2 | 1 | ddp2, ddp3, espgal, ket, kov2, kovsh, olds103t, orlegend, photoy2k |
| 3 | 2 | dmnfrnt, drgw3, dw2001, dwex |
| 4 | 3 | happy6, killbld, svg, theglad |
| 5 | 4 | martmast |
| 6 | 5 | killbldp |

The heavier games run a frame in some 3.5 ms; three frames ahead of martmast take the
application from 37 to 85 percent of a core.

## Decision

- Run-ahead is the application's, one setting for every game: off, or 1 to 3 frames
  (Emulation > Run-ahead), kept in `settings.json`. It is off unless chosen; 1 suits every game.
- The frame kept is run as without run-ahead, its sound played and its state kept for rewind
  ([0018](0018-rewind-keeps-every-frame.md)); the frames ahead are silent, and only the last
  one's picture is shown.
- With a breakpoint, a watchpoint or an audio capture set, no frames are run ahead: they would
  stop, or be heard, where the game kept never went.
- A state loaded gives back the run it was saved from, on every board; a test holds it.

## Consequences

- A game is not given its own setting; the table above says what each can take.
- Every frame costs N + 1 frames of emulation and a save and a load of the state.
- Agents see the frames kept: the protocol's frames, screenshots and sums are untouched by it.
