#!/usr/bin/env python3
"""Runs a game on the emulator and on the RTL simulation side by side, and
compares their memory at chosen frames.

    scripts/compare-with-rtl.py [--game pgm] [--frames 60 600] [--regions WORK_RAM ...]

Both programs are driven over the same JSON-lines protocol
(docs/spec/control-protocol.md): one script, two servers, the same requests.
Each checkpoint reads every region from both and prints where they differ, as
address ranges. The exit status is 0 when every region matched at every
checkpoint.

The simulation runs at about 1.4 frames per second, so 600 frames take some
seven minutes. Its zips come from ../ROMS; the emulator's images from roms/,
built by scripts/make-pgm.sh. `pgm` is the BIOS alone.

--ignore REGION:START-END leaves a range of a region out of the comparison:
dead stack, for one, which holds whatever interrupts pushed and differs
wherever an interrupt arrived at a different instruction.

--program FILE replaces the BIOS's 68000 program with FILE in both, which is
how a PGMTest page (scripts/make-pgmtest.sh) is compared: the simulator's copy
is overwritten with memory.write after loading, the emulator is given FILE's
directory as its first BIOS source.
"""

import argparse
import functools
import json
import pathlib
import subprocess
import sys
import struct
import tempfile
import time
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
WORKSPACE = ROOT.parent
SIM_DIR = WORKSPACE / "Arcade-IGSPGM_MiSTer" / "sim"
# Progress is printed as it happens, also when the output is a file or a pipe.
print = functools.partial(print, flush=True)  # noqa: A001

MAX_READ = 0x100000
WRITE_CHUNK = 0x4000
PROGRAM_NAME = "pgm_p02s.u20"


class Server:
    """A JSON-lines server run as a child process."""

    def __init__(self, name, command, cwd, env=None):
        self.name = name
        self.next_id = 1
        self.process = subprocess.Popen(
            command, cwd=cwd, env=env, text=True,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

    def call(self, method, **params):
        request = {"id": self.next_id, "method": method, "params": params}
        self.next_id += 1
        self.process.stdin.write(json.dumps(request) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            sys.exit(f"{self.name} ended while answering {method}")
        response = json.loads(line)
        if not response.get("ok"):
            sys.exit(f"{self.name}: {method} failed: {response.get('error')}")
        return response["result"]

    def read(self, region, size):
        data = bytearray()
        for address in range(0, size, MAX_READ):
            chunk = min(MAX_READ, size - address)
            result = self.call("memory.read", region=region, address=address, size=chunk)
            data += bytes.fromhex(result["data_hex"])
        return bytes(data)

    def write(self, region, data):
        for address in range(0, len(data), WRITE_CHUNK):
            self.call("memory.write", region=region, address=address,
                      data_hex=data[address:address + WRITE_CHUNK].hex())

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=30)


def decode_png(data):
    """(width, height, rows of RGB bytes) of an 8-bit RGB or RGBA PNG."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    at, idat = 8, b""
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and colour in (2, 6), "only 8-bit RGB and RGBA are read"
            channels = 3 if colour == 2 else 4
        elif kind == b"IDAT":
            idat += body
        at += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left = line[x - channels] if x >= channels else 0
            up = previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + left) & 0xff
            elif kind == 2:
                line[x] = (line[x] + up) & 0xff
            elif kind == 3:
                line[x] = (line[x] + (left + up) // 2) & 0xff
            elif kind == 4:
                guess = left + up - corner
                pa, pb, pc = abs(guess - left), abs(guess - up), abs(guess - corner)
                line[x] = (line[x] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 0xff
        previous = line
        rows.append(bytes(line[i] for i in range(stride) if i % channels < 3))
    return width, height, rows


def compare_pictures(mine, theirs):
    """Differing pixels, and the rows they lie in, of two decoded pictures.

    The simulator's picture is a row lower than the emulator's: its capture
    (sim_video.h) counts the line up on the hblank that ends vblank, before the
    first visible line is stored, so its row 0 is stale and the last visible
    line falls off the bottom. Emulator row y is compared with simulator row
    y + 1, the last row not at all."""
    (width, height, left), (_, _, right) = mine, theirs
    differing, rows = 0, []
    for y in range(height - 1):
        count = sum(1 for x in range(width) if left[y][3 * x:3 * x + 3] != right[y + 1][3 * x:3 * x + 3])
        if count:
            differing += count
            rows.append(y)
    return differing, rows


def differing_ranges(left, right):
    """(start, end) of every run of bytes that differ."""
    ranges = []
    start = None
    for address in range(len(left)):
        if left[address] != right[address]:
            if start is None:
                start = address
        elif start is not None:
            ranges.append((start, address))
            start = None
    if start is not None:
        ranges.append((start, len(left)))
    return ranges


REGION_SIZES = {"WORK_RAM": 0x20000, "VIDEO_RAM": 0x8000, "PALETTE_RAM": 0x2000, "AUDIO_RAM": 0x10000}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", default="pgm")
    parser.add_argument("--frames", type=int, nargs="+", default=[60, 600],
                        help="frames after reset to compare at, ascending")
    parser.add_argument("--regions", nargs="+", default=["WORK_RAM", "VIDEO_RAM", "PALETTE_RAM"],
                        choices=sorted(REGION_SIZES))
    parser.add_argument("--emulator", default=str(ROOT / "build" / "release" / "src" / "pgm_cli" / "pgmemu-cli"))
    parser.add_argument("--bios", default=str(WORKSPACE / "ROMS" / "pgm.zip"))
    parser.add_argument("--ranges", type=int, default=12, help="differing ranges to list per region")
    parser.add_argument("--program", type=pathlib.Path, help="a BIOS program to run instead of the BIOS's own")
    parser.add_argument("--pictures", action="store_true",
                        help="also compare the pictures, and keep both as PNGs in build/compare/")
    parser.add_argument("--ignore", action="append", default=[], metavar="REGION:START-END",
                        help="a hex byte range of a region to leave out, end exclusive; may be repeated")
    args = parser.parse_args()

    ignored = {}
    for item in args.ignore:
        region, span = item.split(":")
        start, end = (int(part, 16) for part in span.split("-"))
        ignored.setdefault(region, []).append((start, end))

    bios_sources = ["--bios", args.bios]
    scratch = tempfile.TemporaryDirectory()
    if args.program:
        override = pathlib.Path(scratch.name) / PROGRAM_NAME
        override.write_bytes(args.program.read_bytes())
        bios_sources = ["--bios", scratch.name] + bios_sources

    emulator = Server("emulator",
                      [args.emulator, "--server", *bios_sources, "--rom-dir", str(ROOT / "roms")],
                      cwd=ROOT)
    simulator = Server("simulator", ["./sim", "--server"], cwd=SIM_DIR,
                       env={"PGM_ROM_DIR": str(WORKSPACE / "ROMS"), "PATH": "/usr/bin:/bin"})
    simulator.call("sim.initialize", headless=True)

    for server in (emulator, simulator):
        server.call("sim.load_game", name=args.game)
    if args.program:
        simulator.write("BIOS_PROG_ROM", args.program.read_bytes())
    for server in (emulator, simulator):
        server.call("sim.reset", cycles=100)

    identical = True
    done = 0
    for frame in args.frames:
        started = time.monotonic()
        runs = {server.name: server.call("sim.run_frames", count=frame - done) for server in (emulator, simulator)}
        done = frame
        print(f"frame {frame}: ran in {time.monotonic() - started:.0f} s; ticks emulator "
              f"{runs['emulator']['ticks_executed']}, simulator {runs['simulator']['ticks_executed']}")
        for region in args.regions:
            mine = emulator.read(region, REGION_SIZES[region])
            theirs = simulator.read(region, REGION_SIZES[region])
            ranges = [(start, end) for start, end in differing_ranges(mine, theirs)
                      if not any(low <= start and end <= high for low, high in ignored.get(region, []))]
            differing = sum(end - start for start, end in ranges)
            print(f"  {region}: {'identical' if not ranges else f'{differing} bytes differ in {len(ranges)} ranges'}")
            identical &= not ranges
            for start, end in ranges[:args.ranges]:
                print(f"    {start:06x}-{end - 1:06x}  emulator {mine[start:min(end, start + 8)].hex()}"
                      f"  simulator {theirs[start:min(end, start + 8)].hex()}")

        if args.pictures:
            pictures = ROOT / "build" / "compare"
            pictures.mkdir(parents=True, exist_ok=True)
            decoded = {}
            for server in (emulator, simulator):
                label = args.program.parent.parent.name if args.program else args.game
                path = pictures / f"{label}-{frame}-{server.name}.png"
                server.call("video.screenshot", path=str(path))
                decoded[server.name] = decode_png(path.read_bytes())
            differing, rows = compare_pictures(decoded["emulator"], decoded["simulator"])
            identical &= differing == 0
            where = f" in rows {rows[0]}-{rows[-1]}" if rows else ""
            print(f"  picture: {'identical' if not differing else f'{differing} pixels differ{where}'}")

    emulator.close()
    simulator.close()
    return 0 if identical else 1


if __name__ == "__main__":
    sys.exit(main())
