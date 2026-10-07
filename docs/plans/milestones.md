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

## M3: Video

*Delivers:*

- Palette and backdrop; the FG layer; the BG layer with row scroll; the mixer.
- Sprites: DMA, mask and colour streams, zoom by the RTL's `scale_pattern`, line buffers,
  priority.
- `video.screenshot` returning a PNG inline; layer, tile and sprite viewers.
- Real-time play from the keyboard.

*Exit:*

- Framebuffer hashes of the BIOS logo and the RTC screen equal the RTL simulation's.
- PGMTest `fg_test`, `bg_test`, `sprite_test` and `video_timing` pass.
- orlegend at frame 1500 equals the RTL simulation's frame.

## M4: Sound

*Delivers:*

- The `z80.h` adapter with the Z80 suite passing; Z80 RAM, I/O map, latches, NMI, bus request,
  reset.
- `Ics2115`: voices, envelopes, timers, IRQ.
- Native-rate output, the resampler, the SDL audio stream with dynamic rate control.
- `audio.capture`; the voice window.

*Exit:*

- PGMTest `z80_ctrl`, `z80_sound_test`, `z80_ics_test` and `ics2115_vol_pan` pass.
- The BIOS jingle and orlegend's attract audio match WAVs captured from the RTL simulation,
  within the tolerance of the scripts in `../Arcade-IGSPGM_MiSTer/audio_tests`.

## M5: The agent's toolset

*Delivers:*

- MCP over stdio in `pgmemu-cli` and over HTTP in `pgmemu`, with tools generated from the method
  table and screenshots returned as images.
- JSON-lines over TCP attached to the GUI.
- Save states, NVRAM, `state.*` and `nvram.*`.
- Trace ring and watchpoints.
- `test.*`.
- A Claude Code project skill for booting, running, inspecting and comparing with the RTL.

*Exit:* a fresh session given only the skill boots orlegend headless, stops at a breakpoint,
reads the sprite list and returns a screenshot.

## M6: The regression suite

*Delivers:*

- Golden frames per game with scripted input (coin, start, a few seconds of play).
- `compare-with-rtl.py` bisecting to frame, line and instruction, and starting from RTL save
  states.
- A headless speed benchmark per game.

*Exit:* `ctest --preset release -L regression` runs every PGMTest page and every golden frame in
under two minutes.

## M7: Protection

*Delivers,* in order:

1. `Asic3`: orlegend fully playable.
2. `Igs025` and `Igs022`: killbld, drgw3, dwex.
3. The ARM7TDMI core: our own or SkyEmu's, decided by a record; the ARM7TDMI suite passing.
4. `Igs027a` type 1: kovsh and photoy2k, then ket, espgal and ddp3.
5. Type 2: kov2, kov2p, ddp2, martmast, dw2001, dwpc.
6. Type 3: dmnfrnt, theglad, svg, killbldp, happy6.

*Exit:* every game the MiSTer README lists as supported reaches gameplay, and its golden frames
are recorded.

## M8: Finish

*Delivers:*

- Input mapping UI for four players and several gamepads.
- Shader presets and integer scaling.
- Rewind and run-ahead.
- A profiling pass against three times real time on every game.
- A macOS app bundle; CI on macOS and Linux.
