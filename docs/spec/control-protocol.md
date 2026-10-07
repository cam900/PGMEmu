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
| `bad_request` | The request is malformed: not JSON, not an object, `id` or `method` missing or of the wrong type, `params` not an object, or a parameter missing, of the wrong type or out of its bounds. |
| `unknown_method` | No method of that name exists. |
| `unknown_game` | `emu.load_game` was given a name with no `<name>.pgm` in the ROM directory, or a name that is not a set name. |
| `load_failed` | A game could not be loaded: the file is not a valid `.pgm` ([pgm-format.md](pgm-format.md)), or the BIOS is missing or wrong. The message names the file and the fault. |
| `no_cartridge` | The method needs a cartridge, and none is loaded. |
| `invalid_region` | No memory region of that name holds anything now. |
| `invalid_range` | The bytes asked for run past the end of the region. |

`unknown_method`, `unknown_game`, `load_failed`, `invalid_region` and `bad_request` mean what
the simulator means by them. `no_cartridge` and `invalid_range` are the emulator's own; the
simulator does not check a range.

## 5. Names shared with the simulator

Every `sim.<name>` method the emulator implements is an alias of `emu.<name>`. The alias answers
exactly what the `emu.` method answers, so a script written against `./sim --server` runs
unchanged.

## 6. Methods

### `emu.status` (alias `sim.status`)

Takes no parameters.

```json
{"id":1,"ok":true,"result":{"version":"devel","game_name":"orlegend"}}
```

| Field | Meaning |
|---|---|
| `version` | The emulator's version: the release tag it was built from, or `devel`. |
| `game_name` | The short name of the loaded cartridge, `pgm` when the BIOS alone is loaded, or `null` before anything is. |

The simulator's other status fields (`running`, `total_ticks`, ...) are added as the state they
describe comes to exist, under the simulator's names.

### `emu.load_game` (alias `sim.load_game`)

Loads a game and the BIOS. Exactly one of the two parameters is given:

| Param | Meaning |
|---|---|
| `name` | A set name: `<name>.pgm` is taken from the ROM directory. `pgm` loads the BIOS alone. A name holds only letters, digits and underscores. |
| `path` | The path of a `.pgm` file. |

```json
{"id":2,"method":"emu.load_game","params":{"name":"orlegend"}}
{"id":2,"ok":true,"result":{}}
```

The ROM directory and the BIOS sources are given when the emulator is started
(`pgmemu-cli --rom-dir DIR --bios PATH...`). The BIOS files are taken from the first BIOS source
that has each, so a directory with PGMTest's `pgm_p02s.u20` named before `pgm.zip` replaces the
program and keeps the rest.

Errors: `unknown_game`, `load_failed`, `bad_request`. A load that fails leaves loaded what was
loaded before it.

### `emu.cartridge_info`

Describes the loaded cartridge. Takes no parameters. `pgmemu-cli --info FILE` prints the same
for a file.

```json
{
  "short_name": "orlegend",
  "long_name": "Oriental Legend / Xiyou Shi E Zhuan (ver. 126)",
  "manufacturer": "IGS",
  "year": "1997",
  "format_version": "0021",
  "hardware": "asic3",
  "roms": [ { "type": "PRG", "mapping": 1048576, "size": 2097152, "crc32": "d5e93543" } ],
  "region_info": { "scheme": "asic3", "default_region": 0, "regions": [ { "id": "WRLD", "value": 0 } ] }
}
```

- `hardware` takes the names in [pgm-format.md §2.1](pgm-format.md#21-hardware).
- `region_info` is `null` when the image has no region block.
- `patch_type` and `patch_offset` appear for the `asic27` scheme, and `default_region` for the
  `asic3` scheme.

Errors: `no_cartridge`.

### `memory.list_regions`

Takes no parameters. Answers the names of the regions that hold something now, as an array.

| Region | Holds |
|---|---|
| `BIOS_PROG_ROM` | `pgm_p02s.u20`, 128 KB |
| `BIOS_TILE_ROM` | `pgm_t01s.rom`, 2 MB |
| `BIOS_MUSIC_ROM` | `pgm_m01s.rom`, 2 MB |
| `CART_PROG_ROM` | The cartridge's PRG |
| `CART_TILE_ROM` | TLE |
| `CART_MUSIC_ROM` | AUD |
| `CART_A_ROM` | SPC |
| `CART_B_ROM` | SPM |
| `CART_ARM_ROM` | EXT |
| `CART_ARM_INT_ROM` | INT (the emulator's own name) |
| `CART_IGS022_ROM` | I22 (the emulator's own name) |

Every region holds its ROM as the `.pgm` file does ([pgm-format.md §3](pgm-format.md#3-the-entry-table)),
and address 0 is the ROM's first byte, whatever its mapping. These are the names the simulator's
`memory.read` accepts, and the two answer the same bytes for the same request. The simulator's
own `memory.list_regions` lists older names that its `memory.read` refuses; the emulator lists the
names that work.

### `memory.read`

| Param | Meaning |
|---|---|
| `region` | A name from `memory.list_regions`. |
| `address` | Byte offset within the region. |
| `size` | Number of bytes, at most 1048576 (1 MB). Larger reads are made in pieces. |

```json
{"id":3,"method":"memory.read","params":{"region":"CART_PROG_ROM","address":0,"size":4}}
{"id":3,"ok":true,"result":{"region":"CART_PROG_ROM","address":0,"data_hex":"82000000"}}
```

`data_hex` holds two lowercase hex digits per byte, in address order.

Errors: `invalid_region`, `invalid_range`, `bad_request`.
