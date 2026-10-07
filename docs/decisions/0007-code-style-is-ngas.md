# 0007: Code style and toolchain are NGA's

**Status:** accepted

## Context

The owner holds code quality and consistent formatting to a strict standard, and already has a
configuration that enforces it in NGA.

## Decision

- `.clang-format` is NGA's, byte for byte.
- `.clang-tidy` has NGA's checks and options unchanged; only comments that described NGA's own
  circumstances were rewritten.
- `scripts/format.sh`, `scripts/tidy.sh` and `scripts/find-llvm-tool.sh` are NGA's.
- The build follows NGA's: CMake 3.25+, Ninja presets `debug`, `release` and `asan`, one
  interface target carrying the warning set (`cmake/Warnings.cmake`, with an option to turn
  warnings into errors), and Catch2 v3 for tests.
- The language is C++23, restricted to the subset Apple clang, GCC and MSVC all implement. A
  `PortabilityChecks.cpp` in the core pins that subset down, as NGA's does.

## Consequences

- One LLVM major (23) formats and tidies both projects. A different major would reformat code
  that is already formatted.
- The naming that `.clang-tidy` enforces (summarised in [CLAUDE.md](../../CLAUDE.md)) also governs
  how RTL signal names become C++ identifiers: `sprite_dma_en` in the RTL becomes `spriteDmaEn`.
  The words are kept, so that a grep across the two repositories still meets.
