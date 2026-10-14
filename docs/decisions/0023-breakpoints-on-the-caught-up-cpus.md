# 0023: Breakpoints on the Z80 and the ARM7 stop the 68000's run, not the CPU

**Status:** accepted

## Context

Breakpoints stopped only the 68000. Debugging the sound driver or a protection program needs them
on the Z80 and on the IGS027A's ARM7 too. JagMCP, the owner's Jaguar emulator built on this
one's control API, has them on all of its processors.

Those two CPUs are not run beside the 68000. They are caught up to it when it reaches them and
when a run ends ([architecture.md](../architecture.md#emulated-time)). So when the Z80 reaches an
address, the 68000 has already run past that moment. There are two ways to stop there:

1. **Stop the caught-up CPU at the breakpoint** and leave it behind the 68000. The next access of
   the 68000 would then find it behind its time, or would have to run it on first. Either changes
   what the 68000 sees compared with a run without the breakpoint, so a breakpoint would change
   the program it is meant to observe.
2. **Let the CPU run on, and only note the breakpoint.** The CPU keeps its registers from the
   first breakpoint it reaches in a run, and the 68000's run stops after the instruction during
   which that happened. The emulation is the one it would have been without the breakpoint.

## Decision

The second. While the Z80 or the ARM7 has a breakpoint, both are caught up after every 68000
instruction, and after each time the 68000 waits (STOP, sprite DMA, reset). The run's answer
says which CPU stopped it, at which address, and its registers as they were there. The CPU itself
has run on to the 68000's time.

A Z80 breakpoint is reached where the Z80 fetches an instruction's first byte; an ARM7 one where
the ARM begins an instruction. Each run keeps the first breakpoint reached and clears it when the
next run begins.

## Consequences

- A breakpoint, reached or not, does not change the emulation. Catching up more often does not
  change it either: each CPU runs to the same times in the same steps, only in more pieces. A test
  holds this for the Z80, and the saved states of orlegend and kov2 after 900 frames are the same
  with and without a breakpoint on either CPU.
- After a Z80 or ARM7 stop, `cpu.get_state` shows the CPU further on than the breakpoint: up to
  the end of the 68000 instruction, or later when the 68000 was waiting. The stop's own
  `registers` are the ones at the breakpoint.
- While such a breakpoint is set, a frame costs more: 2.4 to 3.3 ms on orlegend and 3.4 to 4.8 ms
  on kov2, release build on an M-series Mac, 2026-10-14. That is still well within a frame's 16.9
  ms.
- Watchpoints stay on the 68000 alone. On the Z80 or the ARM7 they would be the same kind of note,
  and could be added the same way when they are needed.
