# Batch scripts

A batch script is a run of the emulator written down: requests of the control protocol
([control-protocol.md](control-protocol.md)), and checkpoints at which the machine is summed.
`pgmemu-cli --batch FILE` runs one headless and unthrottled. The regression suite is such
scripts, each holding the sums it was recorded with: `tests/regression/*.json`.

## The file

A JSON object:

| Field | Meaning |
|---|---|
| `description` | What the script runs, for people. Optional. |
| `bios_program` | A directory holding a `pgm_p02s.u20` to use in place of the BIOS's 68000 program, as a PGMTest page is run; searched before the other BIOS sources. Relative to the working directory. Optional. |
| `steps` | The steps, in order. |
| `golden` | The sums each checkpoint was recorded with, by checkpoint name. Written by `--record`. |

A step is one of:

- a request: `{"method": "emu.run_frames", "params": {"count": 600}}`, as the protocol has it,
  without an `id`;
- a checkpoint: `{"checkpoint": "attract"}`.

```json
{
  "description": "Oriental Legend: the warning, then the attract mode.",
  "steps": [
    { "method": "emu.load_game", "params": { "name": "orlegend" } },
    { "method": "emu.reset", "params": { "cycles": 100 } },
    { "method": "emu.run_frames", "params": { "count": 900 } },
    { "checkpoint": "warning" }
  ],
  "golden": {
    "warning": { "frame": 901, "picture": "a0278f75", "work_ram": "b3c4c0ba", "video_ram": "427b12ad",
                 "palette_ram": "e272cb3f", "audio_ram": "b8ee79a6", "sound": "2d4f53df" }
  }
}
```

## Checkpoints

At a checkpoint the machine is summed:

| Sum | Of |
|---|---|
| `frame` | Frame boundaries passed since power-up: one more than the frames run after an `emu.reset`, as the boundary of the reset itself counts. |
| `picture` | The last complete picture, 448 by 224 RGBA. |
| `work_ram`, `video_ram`, `palette_ram`, `audio_ram` | The regions `memory.read` names so. |
| `sound` | Every stereo frame the ICS2115 produced since the checkpoint before (or since the game was loaded): left, then right, 16-bit little-endian. |

Each is a CRC-32 as eight lowercase hex digits. The emulator is deterministic, so a script's
sums change only when what it emulates does. A script that starts `audio.capture_start` takes
the sound away from the `sound` sum until `audio.capture_stop`.

## Running

```sh
pgmemu-cli --batch FILE --bios ../ROMS/pgm.zip --rom-dir roms            # check against golden
pgmemu-cli --batch FILE --bios ../ROMS/pgm.zip --rom-dir roms --record   # write golden into FILE
```

It prints a line per checkpoint that differs from its golden sums, or one that says they all
match, and exits:

| Status | When |
|---|---|
| 0 | Every checkpoint matches. |
| 1 | A checkpoint differs from its golden sums, has none, or a golden one was not reached. |
| 2 | A step failed, or the file is not a script. |
| 77 | The game or the BIOS program the script needs is not there: CTest's code for a skipped test. |

`--record` overwrites the file's `golden` with the sums of this run. Golden sums are this
emulator's own, so they catch a change, not an error: before recording new ones, compare the run
with the RTL (`scripts/compare-with-rtl.py`).
