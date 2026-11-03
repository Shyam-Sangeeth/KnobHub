"""How fast a real knob draws: pretends to be Knob Hub on the knob's USB port, turns through the Home ring with "key"
messages and sums up the frame times the knob prints. Needs a build with frame timing, and Knob Hub closed (it holds
the port):

    cd knob-os
    $env:PLATFORMIO_BUILD_FLAGS = "-D KNOB_PERF"; pio run -e crowpanel-128 -t upload --upload-port COM3
    python -I ..\\tools\\knob_perf.py COM3

Uses pyserial (PlatformIO's own Python has it: %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe). Flash a normal
build afterwards."""
import json
import re
import statistics
import sys
import time

import serial

PerfLine = re.compile(r"perf frame=(\d+) us flush=(\d+) us px=(\d+) strips=(\d+) cpu=(\d+) free=(\d+)")


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.05
    s.dtr, s.rts = True, False
    s.open()
    offset = -(time.altzone if time.daylight else time.timezone) // 60

    def send(msg):
        s.write((json.dumps(msg, separators=(",", ":")) + "\n").encode())

    keys = (["right"] * 5 + ["left"] * 5) * 2
    plan, frames, buf, next_beat, started, done_at = None, [], b"", 0.0, time.time(), None
    while time.time() - started < 40:
        now = time.time()
        if now >= next_beat:  # the heartbeat that keeps the link up
            send({"t": "hub", "session": "perf", "time": int(now), "offset": offset})
            next_beat = now + 2
        if plan:
            if plan[0][0] <= now:
                send({"t": "key", "input": plan.pop(0)[1]})
                if not plan:
                    done_at = now + 1.5
        elif done_at and now > done_at:
            break
        buf += s.read(4096)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").strip()
            if plan is None and '"t":"hello"' in text:
                plan = [(now + 1.5 + i * 0.7, key) for i, key in enumerate(keys)]
            elif plan is not None and (m := PerfLine.match(text)):
                frames.append(tuple(map(int, m.groups())))
    s.close()

    if plan is None:
        sys.exit("The knob didn't say hello: is Knob Hub closed, and is this the right port?")
    if not frames:
        sys.exit("No frame times: flash a build with -D KNOB_PERF.")
    whole = [f for f in frames if f[2] >= 0.8 * max(f[2] for f in frames)]
    ms = lambda us: f"{us / 1000:.1f} ms"
    print(f"{len(frames)} frames while turning, {len(whole)} of them (nearly) the whole screen")
    print(f"  whole screen: median {ms(statistics.median(f[0] for f in whole))}, slowest {ms(max(f[0] for f in whole))}"
          f"; waiting in flush {ms(statistics.median(f[1] for f in whole))}; {whole[0][3]} strips")
    print(f"  CPU {frames[-1][4]} MHz, internal RAM free {frames[-1][5] // 1024} KB")


if __name__ == "__main__":
    main()
