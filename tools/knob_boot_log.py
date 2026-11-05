"""Restarts the knob through its USB serial port's RTS line and prints the lines it writes while starting that contain
the given text (e.g. "wifi"). Knob Hub must be closed.

    python -I tools/knob_boot_log.py <port> <text>"""
import sys
import time

import serial

port, text = sys.argv[1], sys.argv[2]
s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 0.1
s.dtr, s.rts = False, False
s.open()
s.rts = True
time.sleep(0.1)
s.rts = False
s.close()
buf = b""
end = time.time() + 12
while time.time() < end:
    try:
        s.open()
        break
    except serial.SerialException:
        time.sleep(0.2)
end = time.time() + 6
while time.time() < end:
    try:
        buf += s.read(4096)
    except serial.SerialException:
        break
s.close()
lines = [l for l in buf.decode("utf-8", "replace").splitlines() if text in l]
print("\n".join(lines) if lines else "(nothing matched; last output: %r)" % buf[-300:])
