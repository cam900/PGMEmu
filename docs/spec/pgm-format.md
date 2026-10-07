# The `.pgm` cartridge image

This is what the emulator accepts as a cartridge. The format belongs to PGMBuilder
(`../PGMBuilder/pgm.hpp`, writer in `MameImage.cpp`), which defines it. This document states it
from the reader's side, as of PGMBuilder commit `028194b` (2026-09-18), and says what the reader
refuses. Where the two disagree, PGMBuilder's source is the authority and this document is
corrected.

Only version 0x0021 is read. Older images, including those from PGMBuilder binaries built before
September 2026 (version 0x0020), must be rebuilt (`scripts/make-pgm.sh`).

## 1. Layout

| Bytes | Content |
|---|---|
| 0 to 1023 | Header: the info block (§2), then the entry table (§3), the region block (§4) and strings, packed in that order |
| 1024 onwards | ROM data. Each ROM starts at a multiple of 512 bytes, the gaps zero-filled |

All multi-byte fields are little-endian except the version. The image carries no BIOS: the
motherboard's ROMs come from `pgm.zip` or a directory.

## 2. The info block

| Offset | Size | Field | Read as |
|---|---|---|---|
| 0 | 6 | magic | `IGSPGM`; anything else is refused |
| 6 | 2 | version | **Big-endian** BCD, `00 21` for 00.21; anything else is refused |
| 8 | 4 | infoSize | 76; anything else is refused |
| 12 | 16 | shortName | The MAME set name, NUL-padded; no NUL when it is 16 characters |
| 28 | 4 | manufacturerLongName | Offset of a NUL-terminated string, or 0 for none |
| 32 | 4 | asciiLongName | Offset of the full title when it is ASCII, or 0 |
| 36 | 4 | utf8LongName | Offset of the full title when it is not ASCII, or 0. At most one of the two names is given, or the file is refused |
| 40 | 4 | year | Four characters, not terminated |
| 44 | 4 | hardware | The board the cartridge needs (§2.1) |
| 48 | 4 | genre | Always 0; ignored |
| 52 | 4 | entries | Offset of the entry table |
| 56 | 4 | entriesCount | Number of entries |
| 60 | 4 | cover | Always 0; ignored |
| 64 | 4 | screenshotOffsets | Always 0; ignored |
| 68 | 4 | screenshotOffsetsCount | Always 0; ignored |
| 72 | 4 | regionOffset | Offset of the region block, or 0 for none |

A string offset must point into the header after the info block, at a string that ends before
byte 1024.

### 2.1 Hardware

| Value | Board | Games |
|---|---|---|
| 0 | `pgm`: no protection | bootlegs |
| 1 | `asic3` | orlegend |
| 2 | `igs012_igs025` | drgw2 |
| 3 | `igs022_igs025` | killbld, drgw3 |
| 4 | `arm_type1` | kovsh, photoy2k, ddp3 |
| 5 | `arm_type2` | kov2, martmast, dw2001 |
| 6 | `arm_type3` | theglad, svg, killbldp |
| 7 | `igs028_igs025` | olds |
| 8 | `arm_type1_cave` | ket, espgal |

Value 8 is written by PGMBuilder's database (`MameDB.hpp`) though `pgm.hpp` does not name it. A
value outside the table is kept as read; whether a board can be emulated is the machine's
decision, not the reader's.

## 3. The entry table

`entriesCount` entries of 16 bytes, which must lie within the header:

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | type | What the ROM is (below) |
| 4 | 4 | mapping | The lowest offset of the ROM in its MAME region: where it is mapped |
| 8 | 4 | offset | Where its bytes start in the file |
| 12 | 4 | size | How many bytes it has |

| Type | Name | MAME region | Content |
|---|---|---|---|
| 1 | PRG | `maincpu` | 68k program, **already decrypted** |
| 2 | INT | `prot` | IGS027A internal ROM. A missing dump is a 16 KB block of zeros or a recreated program |
| 3 | EXT | `user1` | IGS027A external ROM, already decrypted |
| 4 | TLE | `tiles` | Background and text tiles |
| 5 | SPM | `sprmask` | Sprite masks (B-ROM) |
| 6 | SPC | `sprcol` | Sprite colours (A-ROM) |
| 7 | AUD | `ics` | ICS2115 samples |
| 8 | I22 | `igs022` / `igs028` | Protection data ROM |
| 9 | I25 | none | IGS025 settings, PGMBuilder's own block (`CustomData.cpp`): variant, default region, then per region a code, a 4-byte game id and a 0xEC-byte table |

**Byte order.** Every ROM is stored as its files hold it, assembled into one block per type in
MAME's layout: byte-interleaved pairs are interleaved, and files loaded one after another are
laid end to end. For PRG this means the 68k's 16-bit words with the **low byte first**, the same
order as the BIOS program `pgm_p02s.u20`. The 68k word at byte address `2n` of the ROM is
`data[2n] | data[2n+1] << 8`.

**The reader refuses:**

- a type outside 1 to 9;
- a type given twice;
- an empty ROM;
- a ROM that starts inside the header or runs past the end of the file.

Entries may come in any order. PGMBuilder writes PRG, INT, EXT, TLE, SPC, SPM, AUD, I22, I25,
leaving out the types a game does not have.

## 4. The region block

The region block tells the game, through its protection chip, which country it is. It is present
only when the game has regions to choose from. It lies within the header and is padded to a
multiple of 4 bytes.

### 4.1 Common part

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | type: 0 ASIC27, 1 IGS025, 2 ASIC3; anything else is refused |
| 1 | 1 | count: number of regions in the table |
| 2 | 2 | offset of the table from the block's start: 8 for ASIC27 and ASIC3, 4 for IGS025; anything else is refused |

### 4.2 Per type

| Type | Offset | Size | Field |
|---|---|---|---|
| ASIC27 | 4 | 2 | patchType: 0 means the region is a big-endian 16-bit word patched into the program |
| ASIC27 | 6 | 2 | patchOffset: where in the program it is patched |
| ASIC3 | 4 | 4 | defaultRegion: the region the game runs as unless told otherwise |

### 4.3 The table

`count` entries of 8 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | agnostic id: a four-character code, its first character in the most significant byte |
| 4 | 4 | region id: the value the protection hands the game |

The codes are `WRLD` (world), `HGKG` (Hong Kong), `JAPN` (Japan), `KREA` (Korea), `TAWN`
(Taiwan), `CHNA` (China), `USOA` (USA) and `SNGP` (Singapore).
