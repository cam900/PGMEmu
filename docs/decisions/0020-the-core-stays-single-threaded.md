# 0020: The core stays on one thread until a machine cannot keep up

**Status:** accepted

## Context

The core runs every chip on one thread ([architecture.md](../architecture.md#emulated-time)):
the 68000 leads an instruction at a time, and the video, the sound side and the ARM are caught up
to it when it reaches them and when a run ends. The question was whether that loop could be split
across threads safely, and whether it would be worth it.

### Where a frame's time goes

Release build, headless, an M-series Mac with 10 cores, 2026-10-08; `sample` over ten seconds of
attract mode:

| ms a frame | orlegend | ket (ARM type 1) | theglad (ARM type 3) |
|---|---|---|---|
| **All** | **2.41** | **3.45** | **4.01** |
| 68000, bus and loop | 0.69 | 0.70 | 0.70 |
| Video: sprites, lines | 0.73: 0.40, 0.30 | 0.59: 0.27, 0.29 | 0.71: 0.37, 0.31 |
| Sound: Z80, ICS2115 | 0.99: 0.54, 0.25 | 0.98: 0.57, 0.20 | 1.00: 0.55, 0.24 |
| ARM7 | 0 | 1.19 | 1.60 |

The 68000 is a fifth to a quarter of a frame. The sound side costs a millisecond in every game,
since the Z80 is stepped a T-state at a time, 143,000 a frame.

### How often the parts meet

68000 accesses a frame, counted over 3,600 frames from power-up:

| Game | To the ARM: writes, reads | Z80 latches: writes, reads | Video: VRAM, palette, register writes |
|---|---|---|---|
| orlegend | none | 0.15, 0.12 | 37 |
| killbld (IGS022) | 0.1, 9 | 0.14, 0.12 | 45 |
| kovsh | 13, 23 | 2.0, 1.6 | 63 |
| ket | 3, 3 | about 0 | 448 |
| kov2 | 108, 421 | 0.12, 0.11 | 53 |
| theglad | 17, 64 | 29, 51 | 77 |

The window onto the Z80's RAM is used in bursts, the sound driver's upload at power-up, not in
play. The video is written to and almost never read back.

### What a split would take

The catch-up model maps onto a pipeline that gives the same frames, sound and sums as one thread,
bit for bit:

- **Video on a thread of its own.** The 68000 keeps its own copy of VRAM, the palette and the
  registers for its reads, and queues every write with its time. The video thread replays the
  queue up to each line's fetch and draws the line, and draws a frame's sprites. The raster, the
  interrupts and the line counter stay with the 68000, as functions of time and registers that it
  needs before every instruction; so does the DMA's copy of the sprite list out of work RAM.
- **The Z80 and the ICS2115 on another.** The 68000's writes to the latches are queued with
  their time. A read of a latch, of the RAM window or of the bus grant waits until the sound
  thread has reached its time. The sound thread never runs past the time the 68000 has published,
  so the order of every exchange is today's. The RTC stays with the 68000.
- **The ARM on a third**, the same way, a FIQ raised by a 68000 write applied at its cycle.
- **With the 68000** stay the IGS022, the IGS025 and ASIC3, which stall it or answer at once, and
  the inputs.
- **Barriers** where today's run ends: a frame's end, a breakpoint, a watchpoint or a condition, a
  save or load of the state (rewind and run-ahead), and a debugger's look.

Threads that run freely and meet every so many microseconds would not be safe: the order of the
exchanges would change from run to run, and the regression sums and the comparison with the RTL
would stop meaning anything.

With four threads and synchronisation for free, a frame would take as long as its slowest part:
orlegend 0.99 ms (sound), 2.4 times faster; ket 1.19 ms (ARM), 2.9 times; theglad 1.60 ms (ARM),
2.5 times. The queues, the cache lines the published time crosses and waking the threads would
leave about twice as fast, on three or four cores. Without an ARM, the Z80 stepped a T-state at a
time is the floor.

### Who would gain

- **The application in real time: no one today.** A frame has 16.9 ms; the heaviest game needs
  4, and the application takes 37 percent of a core, 85 with three frames of run-ahead on
  martmast.
- **The test suite: no one.** Its time was its tests run one after another, 42 independent
  regression processes among them; the test presets now run them in parallel, which took the
  suite from about six minutes to 71 seconds.
- **A slower machine, or more run-ahead**, which no one has asked for yet.

The split would touch the IGS023 (the raster apart from the drawing, a log of writes), the IGS026
(a queue, who owns the RAM window), the IGS027A (a queue), shared parts (single-producer queues, a
published time, spinning then parking), the barriers in `Machine`, and tests that compare threaded
and single-threaded runs of every regression script: some 1,500 to 2,000 lines over two or three
sessions, with the risk of errors in order that only some runs show.

## Decision

- The core stays on one thread, deterministic, as [architecture.md](../architecture.md) has it.
- The owner runs the emulator on other machines first. If one cannot keep a game in real time,
  this record is taken up again.
- Then the order is video first (the least risk, 0.6 to 0.7 ms a frame), then the sound side,
  then the ARM: each as the exact pipeline above, behind a switch that keeps the single thread as
  the reference, and held to the same regression sums.

## Consequences

- A frame costs the sum of its parts, 2.4 to 4 ms here.
- The numbers above are this machine's, on this date; whoever takes this up measures again first:
  `sample` on `pgmemu-cli --benchmark`, and counters on `Bus68k`'s accesses.
