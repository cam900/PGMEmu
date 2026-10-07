# 0001: Documentation regime

**Status:** accepted

## Context

This project is written largely by AI agent sessions. Left alone, such sessions append to
whatever document looks plausible, restate signatures that rot on the first refactor, and leave
status files that duplicate git. The owner's NGA project solved this with a regime that has held
up over two hundred decision records.

## Decision

NGA's regime is adopted whole, as written in `../../NGA/docs/decisions/0001-documentation-regime.md`
and `../../NGA/docs/README.md`:

- Every document answers exactly one question and has an explicit prohibition. The buckets
  are listed in [docs/README.md](../README.md), and a document must be indexed there before it
  may exist.
- Documentation carries *why* and *contracts*; code carries *how*.
- One fact, one place.
- No status files.
- Documentation changes in the same commit as the code that invalidated it.

One bucket is added for this project: `docs/hardware/`. It records where the emulator knowingly
departs from its reference (see [0002](0002-the-fpga-core-is-the-reference.md)), because that
fact cannot be read off either the code or the RTL.

## Consequences

- A milestone plan is a queue in `docs/plans/` that only shrinks; it never says what is done.
- Facts about the hardware are not copied from PGMTech or the RTL into this repository's
  documents; documents link to them. The source of truth stays in one place even across
  repositories.
