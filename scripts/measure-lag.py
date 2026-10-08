#!/usr/bin/env python3
"""Measures how many frames each game takes to show a control pressed.

    scripts/measure-lag.py [GAME...]        # default: every game with a regression script

Each game is brought into play by its script in tests/regression, its controls
let go, and its state saved. From there it is run a frame at a time twice:
once with nothing held, once with a control held from the first frame. The
first frame whose picture differs is the game's answer to the control; the
earliest over left, right, up, down and button 1 is reported. Where no control
changes anything, the player being off the screen or the game between scenes,
the game is run two seconds on and asked again, ten times at most. The region
variants of a script (killbld-japan) are left out.

A game that answers on frame N gains nothing from run-ahead beyond N - 1
frames: more makes it skip pictures (docs/decisions/0019-run-ahead.md).
"""

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROBES = ["left", "right", "up", "down", "button1"]
CONTROLS = PROBES + ["button2", "button3", "button4", "start", "coin"]
FRAMES = 15
ATTEMPTS = 10
ATTEMPT_FRAMES = 120


class Emulator:
    def __init__(self, cli, bios, roms, states):
        self.process = subprocess.Popen(
            [cli, "--server", "--bios", bios, "--rom-dir", roms, "--state-dir", states],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self.next_id = 0

    def call(self, method, **params):
        self.next_id += 1
        self.process.stdin.write(json.dumps({"id": self.next_id, "method": method, "params": params}) + "\n")
        self.process.stdin.flush()
        answer = json.loads(self.process.stdout.readline())
        if not answer.get("ok"):
            raise RuntimeError(f"{method}: {answer.get('error')}")
        return answer["result"]

    def picture(self):
        return hashlib.sha1(json.dumps(self.call("video.screenshot")).encode()).hexdigest()

    def pictures(self, held):
        """The pictures of FRAMES frames from the saved state, `held` down throughout."""
        self.call("state.load", filename="lag")
        if held:
            self.call("input.set", name=held)
        shots = []
        for _ in range(FRAMES):
            self.call("emu.run_frames", count=1)
            shots.append(self.picture())
        if held:
            self.call("input.clear", name=held)
        return shots

    def close(self):
        self.process.stdin.close()
        self.process.wait()


def measure(emulator, script):
    for step in json.loads(script.read_text())["steps"]:
        if "method" in step:
            emulator.call(step["method"], **step.get("params", {}))
    for control in CONTROLS:
        emulator.call("input.clear", name=control)
    answers = {}
    for _ in range(ATTEMPTS):
        emulator.call("emu.run_frames", count=ATTEMPT_FRAMES)
        emulator.call("state.save", filename="lag")
        still = emulator.pictures(None)
        for probe in PROBES:
            moved = emulator.pictures(probe)
            answers[probe] = next((n + 1 for n, (a, b) in enumerate(zip(still, moved)) if a != b), None)
        seen = [n for n in answers.values() if n is not None]
        if seen:
            return min(seen), answers
        # On from where the probes began, nothing held.
        emulator.call("state.load", filename="lag")
        emulator.call("emu.run_frames", count=FRAMES)
    return None, answers


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("games", nargs="*")
    parser.add_argument("--cli", default=str(ROOT / "build/release/src/pgm_cli/pgmemu-cli"))
    parser.add_argument("--bios", default=str(ROOT.parent / "ROMS/pgm.zip"))
    parser.add_argument("--rom-dir", default=str(ROOT / "roms"))
    args = parser.parse_args()

    scripts = sorted(p for p in (ROOT / "tests/regression").glob("*.json")
                     if p.stem != "pgm" and "-" not in p.stem)
    if args.games:
        scripts = [p for p in scripts if p.stem in args.games]

    print(f"{'game':16} {'answers on':>10}  {'run-ahead':>9}  " + " ".join(f"{p:>7}" for p in PROBES))
    with tempfile.TemporaryDirectory() as states:
        for script in scripts:
            emulator = Emulator(args.cli, args.bios, args.rom_dir, states)
            try:
                first, answers = measure(emulator, script)
            finally:
                emulator.close()
            shown = "-" if first is None else str(first)
            ahead = "-" if first is None else str(first - 1)
            print(f"{script.stem:16} {shown:>10}  {ahead:>9}  "
                  + " ".join(f"{'-' if answers[p] is None else answers[p]:>7}" for p in PROBES), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
