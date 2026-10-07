# 0005: One control API, spoken in the RTL simulator's protocol

**Status:** accepted

## Context

The emulator will be driven by AI agent sessions as much as by a person: booting games,
stepping, reading memory, taking screenshots, and comparing against the RTL. Gearlynx answers
this with an MCP server. The MiSTer Verilator simulation answers it with a JSON-lines server on
stdio (`../Arcade-IGSPGM_MiSTer/docs/sim-server.md`). Each answer suits different users:

- MCP gives an agent tool discovery, typed parameters and images it can see.
- A line protocol is what a Python harness, CI and a differential test want: cheap per call,
  trivially scriptable, and no session handshake.

## Decision

Every capability is a method of one dispatcher in the core library. Each method takes JSON
`params` and returns a JSON `result`.

- The method names and the wire format are a superset of the RTL simulator's protocol. Where
  the simulator has a method, the emulator's method has the same name and meaning.
- Several transports reach the one dispatcher: JSON-lines on stdio (headless) and on TCP
  (attached to the GUI), and MCP on stdio (headless) and over HTTP (attached to the GUI). The MCP
  tool list is generated from the dispatcher's method table.
- The GUI's debugger windows read state through the same structures the methods return.

The protocol is specified in `docs/spec/control-protocol.md`, which is written with the first
method.

## Consequences

- One script drives both the emulator and the RTL simulation. Comparing them frame by frame,
  then bisecting to the first diverging line or instruction, is the main tool for making the
  emulator match the core.
- Supporting both MCP and the line protocol costs two thin adapters, not two APIs, and neither
  can drift from the other.
- Where the simulator's method is shaped by Verilator (`signal.*`), the emulator answers with
  the closest emulated state and documents the mapping in the spec.
