# The control protocol

This is what a client sends to the emulator and what it gets back, whatever carries the bytes.
Why there is one protocol, and why it is the RTL simulator's, is
[0005](../decisions/0005-one-control-api.md). The simulator's own description,
`../Arcade-IGSPGM_MiSTer/docs/sim-server.md`, is the baseline. This document says where the
emulator matches it and what it adds.

## 1. Framing (JSON-lines)

`pgmemu-cli --server` reads requests on stdin and writes responses on stdout.

- One request is one line holding one JSON object. One response is one line holding one JSON
  object, written in the order the requests arrived and flushed after each.
- Blank lines are skipped and are not answered.
- stdout carries responses and nothing else. Logs go to stderr.
- Serving ends when stdin ends.

## 2. Requests

```json
{"id":1,"method":"emu.status","params":{}}
```

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | A non-negative integer, echoed in the response. The protocol does not require ids to be unique or increasing; a client that wants to match responses keeps them so. |
| `method` | yes | The method's name. |
| `params` | no | An object. A method that takes no parameters is given `{}` when it is left out. |

## 3. Responses

A request that succeeded:

```json
{"id":1,"ok":true,"result":{"version":"devel"}}
```

A request that failed:

```json
{"id":1,"ok":false,"error":{"code":"unknown_method","message":"Unknown method: emu.nonsense"}}
```

- Keys appear in the order shown, as the simulator writes them.
- `error.code` is stable and meant for programs; `error.message` is for people and may change.
- A request whose `id` cannot be read is answered under `id` 0. That covers text that is not
  JSON, JSON that is not an object, and an `id` that is missing, negative or not an integer.
- Every request is answered. A malformed one never ends the session.

## 4. Error codes

| Code | Meaning |
|---|---|
| `bad_request` | The request is malformed: not JSON, not an object, `id` or `method` missing or of the wrong type, or `params` not an object. |
| `unknown_method` | No method of that name exists. |

Codes the simulator also defines are added here with the first method that can fail with them.

## 5. Names shared with the simulator

Every `sim.<name>` method the emulator implements is an alias of `emu.<name>`. The alias answers
exactly what the `emu.` method answers, so a script written against `./sim --server` runs
unchanged.

## 6. Methods

### `emu.status` (alias `sim.status`)

Takes no parameters.

```json
{"id":1,"ok":true,"result":{"version":"devel"}}
```

| Field | Meaning |
|---|---|
| `version` | The emulator's version: the release tag it was built from, or `devel`. |

The simulator's status fields (`running`, `total_ticks`, `game_name`, ...) are added as the
state they describe comes to exist, under the simulator's names.
