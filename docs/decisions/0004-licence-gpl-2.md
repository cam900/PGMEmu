# 0004: Licence is GPL-2.0

**Status:** accepted; its PGMBuilder point superseded by [0021](0021-pgmbuilder-is-not-a-source-of-code.md)

## Context

The owner has no preference between a permissive licence and the GPL. The constraint comes from
what the emulator is derived from. Per [0002](0002-the-fpga-core-is-the-reference.md) its hardware
modules are ports of the MiSTer core, which ships the GPL-2 text and states no "or later". A
port is a derivative work.

## Decision

PGMEmu is released under GPL-2.0.

- **Gearlynx** is GPL-3, which cannot be combined with GPL-2-only code. It is a design reference
  only, and no code is copied from it.
- **PGMBuilder** is the owner's own code and may be reused. In practice it is a tool that
  produces `.pgm` files, not a source of code.
- **Third-party libraries** are all MIT or zlib and are compatible
  ([0006](0006-dependencies.md)).

## Consequences

- Shell code that Gearlynx already has, such as the shader chain, the memory editor and the MCP
  transport, is written anew or taken from permissive libraries (for example ImGui's
  `imgui_memory_editor`).
- If the MiSTer core's author confirms "GPL-2.0 or later", a new record may move the project to
  GPL-3 and lift this restriction.
