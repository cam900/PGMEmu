# 0011: Layers are drawn a line at a time, sprites a frame at a time

**Status:** accepted

## Context

The RTL produces the picture dot by dot. Its layers fetch from VRAM and the tile ROM ahead of
the beam:

- The text layer fetches a line's 57 tiles from dot 638 of the line before.
- The background streams a line's tiles while it is shown.
- The palette is read for every dot.

Its sprite engine copies the sprite list out of work RAM at line 221, then draws the next frame
into 32 line buffers, ahead of the beam as buffer space allows. An emulator stepping dot by dot
would be exact and slow. The question was how much of that timing a game can see.

## Decision

- **A line is drawn whole at dot 638 of the line before**, when the RTL starts fetching it, from
  the registers, VRAM and palette of that moment.
- **The sprite layer of a frame is drawn whole when the DMA copies the list**, at line 221 of the
  frame before. It is a function of that copy and the ROMs alone: the RTL's drawing reads nothing
  else, and its line buffers only decide when, not what. The new sprites take over where vertical
  blank begins, as they do on the RTL.
- The DMA holds the 68000 off the bus for four master ticks a word copied, plus four, as
  `igs023_sprite.sv` takes them.

## Consequences

- A write the RTL would see within a line, or a line later, is seen at the line's start. That
  applies to a palette change mid-line, to VRAM rewritten while the background streams, and to
  the scroll registers. Nothing tested so far depends on it: the BIOS, PGMTest's video pages and
  orlegend's attract mode match the RTL simulation pixel for pixel.
- A frame with more sprites than the RTL can draw in time, which leaves the bottom lines short,
  comes out whole here.
- Rendering costs a few hundred microseconds a frame, so a frame runs several times faster than
  real time.
