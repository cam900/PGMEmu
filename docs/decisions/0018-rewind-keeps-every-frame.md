# 0018: Rewind keeps a whole state for every frame

**Status:** accepted

## Context

A rewind goes back through what was played, a frame at a time, while a key is held. The states
it goes back to must be kept as the game runs. They can be kept whole, as differences from the
one before, or one every so many frames with the frames between run again. The latter two cost
less memory and more of the CPU: a difference is computed for every frame and applied for every
step back, and a sparse history runs frames to reach the one wanted.

A state is 1.4 to 1.8 MB: work RAM, the video chip's RAM and its two frame buffers, the sound
chips, and on the ARM boards the ARM's RAM. `Machine::saveState()` takes some 0.1 ms and
`loadState()` 0.12 ms, measured on an M-series Mac against 2 to 3.5 ms for a frame. The owner
chose the CPU over memory.

## Decision

- The emulation thread keeps `Machine::saveState()` after every frame, whole and uncompressed, for
  the last 30 seconds: some 1,800 states, up to 3 GB.
- While the Rewind hotkey is held, each frame due loads the state before the last kept, drops the
  last, and shows the picture that state holds; the sound is silent. Pause does not stop it.
  Let go, the game runs on from there.
- The history is forgotten when another game or region is loaded, whose ROMs differ, and is not
  kept at all when Emulation > Rewind is off.
- A rewind is the application's: an agent has `state.save` and `state.load`.

## Consequences

- With rewind on, the application holds up to 3 GB more; turned off, nothing.
- A request that changes the machine, such as `emu.reset` or `state.load` from an agent, is gone
  back through like any frame.
