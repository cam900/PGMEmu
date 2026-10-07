# 0009: Moira's configuration is ours

**Status:** accepted

## Context

Moira is configured at compile time through `MoiraConfig.h`, which its sources include by name.
Upstream ships that file with defaults chosen for its own test runner:

- `MOIRA_MIMIC_MUSASHI` on, which reproduces Musashi's deviations from the 68000;
- `MOIRA_PRECISE_TIMING` off, so `sync()` is called once per instruction rather than before each
  bus access.

Neither suits a machine whose reference CPU is the cycle-exact fx68k
([0002](0002-the-fpga-core-is-the-reference.md)). Editing the file in `libextern/` would break
[0006](0006-dependencies.md)'s rule that vendored files are unmodified. The file has no
`#ifndef` guards, so its values cannot be overridden from the command line either.

## Decision

- The configuration is a file of ours, `src/pgm_core/moira/MoiraConfig.h`. Moira is designed to
  take this file from its client, so supplying it is configuring the library, not patching it.
- Upstream's `MoiraConfig.h` is not carried in `libextern/Moira`. Ours is put on the include path
  of the `moira` target.
- `MoiraConfigCheck.cpp` fails the build if any other configuration is found. Moira includes the
  file with quotes, so a copy of upstream's beside its sources would otherwise win silently.

The settings, each explained in the file:

| Setting | Value |
|---|---|
| precise timing | on |
| virtual client API | on |
| address errors | on |
| function codes | off |
| 68020 instruction cache | off |
| disassembler | on |
| Musashi mimicry | off |

## Consequences

- Updating Moira means checking upstream's `MoiraConfig.h` for new settings and adding them to
  ours. `libextern/README.md` lists this in the update procedure.
- `sync()` runs before every bus access. It is the price of charging wait states where the RTL
  charges them. If profiling shows the virtual client interface to be a cost, switching it off is a
  change to this file plus a non-virtual implementation of the callbacks.
