"""Stands in for Knob Hub on the knob's USB port and writes down everything the knob sends, with times (seconds since
the start), to a file. Knob Hub must be closed (it holds the port).

    python -I tools/knob_listen.py <port> <seconds> <out.txt>"""
import json
import sys
import time

import serial

port, seconds, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 0.05
s.dtr, s.rts = True, False
s.open()
offset = -(time.altzone if time.daylight else time.timezone) // 60
start, beat, buf = time.time(), 0.0, b""
with open(out, "w", encoding="utf-8") as f:
    while time.time() - start < seconds:
        now = time.time()
        if now >= beat:
            s.write((json.dumps({"t": "hub", "session": "listen", "time": int(now), "offset": offset}, separators=(",", ":")) + "\n").encode())
            beat = now + 2
        buf += s.read(4096)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").strip()
            if text and len(text) < 400:
                f.write(f"{now - start:7.2f}  {text}\n"); f.flush()
s.close()
