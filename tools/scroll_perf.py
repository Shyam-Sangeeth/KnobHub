"""Frame times on a real knob for scrolling text: the same list with short labels and with long ones (the highlighted
row scrolls), idle and while turning. Pretends to be Knob Hub on the knob's USB port. Needs a build with frame timing
and Knob Hub closed, like knob_perf.py (see its header); flash a normal build afterwards.

    python -I tools/scroll_perf.py [COM7]"""
import json
import re
import statistics
import sys
import time

import serial

PerfLine = re.compile(r"perf frame=(\d+) us flush=(\d+) us px=(\d+) strips=(\d+) cpu=(\d+) free=(\d+)")
port = sys.argv[1] if len(sys.argv) > 1 else "COM7"
s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 0.02
s.dtr, s.rts = True, False
s.open()
offset = -(time.altzone if time.daylight else time.timezone) // 60
buf, frames, hello, next_beat = b"", [], False, 0.0


def send(msg):
    s.write((json.dumps(msg, separators=(",", ":")) + "\n").encode())


def pump(seconds, keys_every=None):
    """Reads frame lines for a while (keeping the link up), turning right/left every keys_every seconds."""
    global buf, hello, next_beat
    end, got, next_key, i = time.time() + seconds, [], time.time() + (keys_every or 0), 0
    while time.time() < end:
        now = time.time()
        if now >= next_beat:
            send({"t": "hub", "session": "perf", "time": int(now), "offset": offset})
            next_beat = now + 2
        if keys_every and now >= next_key:
            send({"t": "key", "input": "right" if (i // 3) % 2 == 0 else "left"})
            i += 1
            next_key = now + keys_every
        buf += s.read(4096)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").strip()
            if '"t":"hello"' in text:
                hello = True
            elif m := PerfLine.match(text):
                got.append(tuple(map(int, m.groups())))
    return got


def report(name, got, seconds):
    if not got:
        print(f"{name:34} no frames drawn")
        return
    t = [f[0] / 1000 for f in got]
    px = [f[2] for f in got]
    busy = sum(f[0] for f in got) / 1e6 / seconds * 100
    print(f"{name:34} {len(got) / seconds:5.1f} frames/s  median {statistics.median(t):5.1f} ms  slowest {max(t):5.1f} ms  "
          f"median {int(statistics.median(px)):6} px  drawing {busy:4.1f}% of the time")


pump(3)
if not hello:
    sys.exit("The knob didn't say hello: is Knob Hub closed, and is this the right port?")
send({"t": "apps", "items": [{"id": "demo", "name": "Demo", "subtitle": "", "icon": "x", "color": "#8B7CF6"}],
      "order": ["demo", "clock", "settings"]})
pump(0.5)
send({"t": "key", "input": "left"})
pump(0.8)
send({"t": "key", "input": "press"})
pump(1.0)


def rows(long):
    tail = " with a label far too long to fit on the row" if long else ""
    return [{"id": c, "label": f"Row {c.upper()}{tail}", "value": "On"} for c in "abcdef"]


for long in (False, True):
    kind = "long labels" if long else "short labels"
    send({"t": "screen", "app": "demo", "screen": kind, "template": "list", "data": {"title": "ROWS", "items": rows(long)}})
    pump(2.5)  # drawn, and past the marquee's first pause
    report(f"{kind}, idle 6 s", pump(6), 6)
    report(f"{kind}, turning every 0.4 s", pump(6, keys_every=0.4), 6)
s.close()
