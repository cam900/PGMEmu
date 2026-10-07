# Where the emulator differs from the RTL, and the RTL from the board

The reference is the RTL ([0002](../decisions/0002-the-fpga-core-is-the-reference.md)). This
file lists every place where the emulator knowingly departs from it, and every behaviour of the
RTL the emulator copies although the board does not behave so. An entry goes in the commit that
makes it true and comes out in the commit that makes it false.

## RTL behaviour copied, though the board differs

| What | The RTL | The board | Where |
|---|---|---|---|
| ROM space without a cartridge | Reads the BIOS's SDRAM region: the program, zeros to 1 MB, the BIOS tiles at 1 MB and its samples at 3 MB. | Open bus. | `RomSpace` |
| RTC | Starts from zero rather than the date. Its "second" passes every 65536 pulses of a clock derived from the Z80's, about 1.77 s. | A V3021 with its own 32.768 kHz crystal. | `V3021` |

## The emulator, where it is not yet the RTL

| What | The emulator | The RTL | Until |
|---|---|---|---|
| VRAM contention | A fixed number of wait states per 68000 cycle to VRAM, from `igs023.sv`'s state machine. | The cycle also waits while the text and background layers fetch from VRAM. | M3 |
| Sprite DMA | Not performed. | At line 221 it takes the bus from the 68000 and reads the sprite list. | M3 |
| Z80 | Absent. A bus request is granted at once. | A running Z80 grants it a few cycles later, and its sound driver writes the latches the BIOS reads. | M4 |
| ROM read timing | No delay, as on the board. | A 256-line cache in front of SDRAM freezes the 68000 on a miss and catches up after; latency varies with video and audio contention, and the simulator's SDRAM model adds random delays. | not planned ([0010](../decisions/0010-rom-timing.md)) |

## What comparing with the RTL shows today

`scripts/compare-with-rtl.py` runs the same requests on both. With the BIOS alone, VRAM and
palette RAM are identical at frames 60 and 600. Work RAM differs in two places:

- One BIOS countdown (`timer_slots`, 0x801550) is a vblank further on in the emulator. The
  emulator finishes the boot about 1,800 master ticks (35 µs) sooner. The rows above are the
  candidates, VRAM contention first.
- Bytes below the stack pointer hold what interrupts pushed earlier, and differ wherever an
  interrupt arrived at a different instruction. They are dead, and `--ignore` leaves them out.

With PGMTest's `system_basics` page, whose screen prints the vblank, IRQ4 and frame counters,
VRAM and palette RAM are identical at frames 30 and 120, and work RAM differs only in dead stack.
