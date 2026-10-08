# Where the emulator differs from the RTL, and the RTL from the board

The reference is the RTL ([0002](../decisions/0002-the-fpga-core-is-the-reference.md)). This
file lists every place where the emulator knowingly departs from it, and every behaviour of the
RTL the emulator copies although the board does not behave so. An entry goes in the commit that
makes it true and comes out in the commit that makes it false.

## RTL behaviour copied, though the board differs

| What | The RTL | The board | Where |
|---|---|---|---|
| ROM space without a cartridge | Reads the BIOS's SDRAM region: the program, zeros to 1 MB, the BIOS tiles at 1 MB and its samples at 3 MB. | Open bus. | `RomSpace` |
| VRAM size | 32 KB, but the background map is folded into its first 4 KB and 0x6000-0x6FFF onto the text map (`vram_phys`). PGMTest's `vram_write_test` fails on it, identically on the RTL and the emulator. | 32 KB, every byte its own. | `Igs023` |
| Text layer's fetch | Stops after 464 cycles of the 33 MHz clock whether or not all 57 tiles arrived; with the simulator's SDRAM latency the last tiles of a line can be left from an earlier fetch. The emulator always draws them fresh, so PGMTest's `fg_test` differs from the RTL in the rightmost column only. Probable cause, from reading `igs023_fg.sv`. | Fetches every tile. | not reproduced |
| RTC | Starts from zero rather than the date. Its "second" passes every 65536 pulses of a clock derived from the Z80's, about 1.77 s. | A V3021 with its own 32.768 kHz crystal. | `V3021` |

## The emulator, where it is not yet the RTL

| What | The emulator | The RTL | Until |
|---|---|---|---|
| VRAM contention | Wait states from `igs023.sv`'s byte-wide state machine, and a wait to the end of the text layer's fetch window. The background's reads of a few dots per tile are not counted. | The same, and the background's reads. | open ([question](../open-questions.md)) |
| When the picture is read | A line at its start, sprites a frame at a time ([0011](../decisions/0011-video-is-drawn-by-line-and-by-frame.md)). | Dot by dot, sprites as line buffers free. | not planned |
| ASIC3's region | The cartridge's default region. | Always 0, the world. The two agree for every image built from the workspace's sets. | — |
| Z80 | Absent. A bus request is granted at once. | A running Z80 grants it a few cycles later, and its sound driver writes the latches the BIOS reads. | M4 |
| ROM read timing | No delay, as on the board. | A 256-line cache in front of SDRAM freezes the 68000 on a miss and catches up after; latency varies with video and audio contention, and the simulator's SDRAM model adds random delays. | not planned ([0010](../decisions/0010-rom-timing.md)) |

## What comparing with the RTL shows today

`scripts/compare-with-rtl.py` runs the same requests on both.

- **The BIOS alone:** work RAM, VRAM, palette RAM and the picture are identical at frames 60
  and 600.
- **orlegend:** VRAM, palette RAM and the picture are identical at frames 300, 900, 1200 and
  1500; work RAM is identical at 300 and 900, and later differs in dead stack only.
- **PGMTest's video pages:** `bg_test`, `sprite_test`, `video_timing` and `system_basics` match
  the RTL picture pixel for pixel. `fg_test` differs in its rightmost column, for the reason in
  the table above.
- **Dead stack:** bytes below the stack pointer hold what interrupts pushed earlier, and differ
  wherever an interrupt arrived at a different instruction. `--ignore` leaves them out; orlegend's
  stack reaches down to 0x81F000.

Timing checkpoints in the BIOS's boot still show the emulator running some loops about 0.03 %
faster than the RTL, mostly while it copies ROM into the Z80's RAM and verifies it. Since the
interrupt acknowledge's E-clock wait (`M68k::willInterrupt`) and the text layer's VRAM window
were modelled, no tested outcome depends on it; the open question on
[timing drift](../open-questions.md) keeps it in view.

The simulator's screenshots are a row lower than the emulator's (`scripts/compare-with-rtl.py`
says why), which the comparison allows for.
