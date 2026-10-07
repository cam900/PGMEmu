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
import tempfile
import time

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

    emulator.close()
    simulator.close()
    return 0 if identical else 1


if __name__ == "__main__":
    sys.exit(main())
