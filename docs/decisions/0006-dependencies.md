# 0006: Code that decides emulated behaviour is vendored, tooling is fetched

**Status:** accepted

## Context

NGA draws the line between its two kinds of dependency in its record 0021: code that decides
what the suite asserts is carried in the tree, and everything else is fetched. The same line
applies here, with "what the emulated machine does" in place of "what the suite asserts".

## Decision

- **Vendored** under `libextern/`, unmodified, at a named upstream commit, with a
  `CMakeLists.txt` of ours and an entry in `libextern/README.md`:
  - Moira (68000)
  - `z80.h` and `z80dasm.h` from floooh/chips

  These decide what the machine does, so they are reviewed in the tree like the rest of the
  emulator.
- **Fetched** with `FetchContent`, each pinned to an exact tag: SDL3, Dear ImGui (docking
  branch) and `imgui_memory_editor`, CLI11, spdlog, nlohmann/json, miniz, and Catch2 for the
  tests.
- Vendored code is not formatted, not tidied, and is built without the project's warning set.
  Its include directories are `SYSTEM` for consumers.

## Consequences

- Updating a CPU core is a commit that shows upstream's diff.
- A clean configure needs the network once per build directory, as in NGA.
- Whether a fetched library's ImGui backend files are built from its tree or carried in it is
  decided in M0, when SDL3 and ImGui are first wired in.
