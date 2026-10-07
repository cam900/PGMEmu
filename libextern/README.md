# Vendored libraries

This is third-party code carried in the tree rather than fetched at configure time.
[0006](../docs/decisions/0006-dependencies.md) says what qualifies and why the two kinds of
dependency are handled differently. This file is the inventory and the procedure.

## The rules

- One directory per library, holding upstream's files **unmodified** and a `CMakeLists.txt` of
  ours. A change upstream needs goes upstream. A change we need is a reason to reconsider the
  library, not to fork it silently.
- Upstream's files are not formatted, not tidied, and built without the project's warning set.
  `scripts/format.sh`, `scripts/tidy.sh` and `.clang-tidy` all stop at `src/` and `tests/`, and
  the include directory is `SYSTEM` on the consumer's side. A library directory therefore has no
  `src/` subdirectory, because the tidy header filter would match it.
- The table below is updated **in the same commit** as the files it describes.

## Inventory

| Library | Upstream | Commit | Licence | Local changes | Used by |
|---|---|---|---|---|---|
| `Moira` | https://github.com/dirkwhoffmann/Moira | `ce28239b0507ebfe1bf2ad2b9c9252dc2f2cc031` (v3.0-15, 2026-09-23) | MIT | none; `MoiraConfig.h` not carried | `pgm_core`: the 68000, see [0003](../docs/decisions/0003-cpu-cores.md) |
| `chips` | https://github.com/floooh/chips | `9e88298ce56319953ac7a43213a1120359f7a3a6` (2026-09-26) | zlib | none | `pgm_core`: the Z80, see [0003](../docs/decisions/0003-cpu-cores.md) |

Of Moira, the contents of upstream's `Moira/` directory are carried, except:

- **`MoiraConfig.h`.** Moira expects its client to supply this file, so ours is
  `src/pgm_core/moira/MoiraConfig.h`
  ([0009](../docs/decisions/0009-moira-configuration.md)).
- **`CMakeLists.txt`.** It is replaced by ours, which passes the same flags.

Upstream's test runner, documentation, Xcode project and the Musashi and binutils copies it
tests against are not carried.

Of chips, only `chips/z80.h`, `util/z80dasm.h` and `LICENSE` are carried. The other chips, the
systems and the UI are not.

## Updating one

1. Clone upstream at the new commit into a scratch directory, outside this tree.
2. Replace the library's files with upstream's, file by file, keeping the subset listed above.
   **Never copy Moira's `MoiraConfig.h`.** `src/pgm_core/src/cpu/MoiraConfigCheck.cpp` fails the
   build if it is copied anyway.
3. Diff upstream's own `CMakeLists.txt` against the flags in ours and carry over what changed.
   For Moira, also diff upstream's `MoiraConfig.h` against its previous version, and decide each
   new setting in ours.
4. Update the commit column above, then build and run the suite in the same commit.
