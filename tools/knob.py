"""Knob OS through knob_preview.dll (`pio run -e preview`), for checks and screenshots without a knob or Knob Hub.

    from knob import Knob
    k = Knob(board="crowpanel-1.28", screen=240)  # a fresh Knob OS: each Knob loads its own copy of the DLL
    k.connect()
    k.send({"t": "apps", "items": [...], "order": [...]})
    k.turn_left(); k.save_png("home.png")

The DLL is %USERPROFILE%\\.cache\\knob-os\\build\\preview\\knob_preview.dll, or KNOB_PREVIEW_DLL. Messages the knob
sends are collected in Knob.sent; icon requests are answered with a test icon (a ring with a dot). Run scripts with
`python -I`."""
import base64
import ctypes
import json
import math
import os
import shutil
import struct
import tempfile
import time
import zlib

DEFAULT_DLL = os.path.join(os.environ.get("USERPROFILE", ""), ".cache", "knob-os", "build", "preview", "knob_preview.dll")
SEND = ctypes.WINFUNCTYPE(None, ctypes.c_char_p)
RIGHT, LEFT, PRESS, BACK = 0, 1, 2, 3
EFFECTS = ["tick", "bump", "click", "alert"]
DETENTS = ["fine", "medium", "firm"]


def test_icon(name, px):
    a8 = bytearray(px * px)
    c = (px - 1) / 2
    for y in range(px):
        for x in range(px):
            d = math.hypot(x - c, y - c)
            if abs(d - px * 0.4) < px * 0.07 or d < px * 0.12:
                a8[y * px + x] = 255
    return {"t": "icon", "name": name, "size": px, "w": px, "h": px, "hash": f"test-{name}",
            "a8": base64.b64encode(bytes(a8)).decode()}


class Knob:
    def __init__(self, board=None, screen=240, dll=None):
        """board: an id from knob-os/src/core/boards.cpp (the 1.28" CrowPanel by default). screen: 240 or 360 draws
        exactly what a knob of that size shows; 480 is Knob Hub's preview."""
        src = dll or os.environ.get("KNOB_PREVIEW_DLL") or DEFAULT_DLL
        path = os.path.join(tempfile.mkdtemp(), "knob_preview.dll")  # Knob OS starts once per DLL: a copy each
        shutil.copy(src, path)
        kp = self.kp = ctypes.CDLL(path)
        kp.kp_link_line.argtypes = [ctypes.c_char_p]
        kp.kp_board.argtypes = [ctypes.c_char_p]
        kp.kp_frame.restype = ctypes.c_void_p
        kp.kp_frame.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
        kp.kp_motor.restype = ctypes.c_uint32
        kp.kp_motor.argtypes = [ctypes.POINTER(ctypes.c_int)] * 4
        kp.kp_leds.restype = ctypes.c_uint32
        self.size = kp.kp_screen(screen)
        if not self.size:
            raise ValueError(f"no fonts for a {screen} px screen")
        if board and kp.kp_board(board.encode()) <= 0:
            raise ValueError(f"unknown board {board}")
        self.sent = []
        self.answer_icons = True
        self._icons = []  # icon requests to answer, outside the DLL's callback
        self._callback = SEND(self._on_send)  # kept alive: the DLL holds on to it
        kp.kp_start(self._callback)
        self.run(0.1)

    def _on_send(self, line):
        msg = json.loads(line.decode())
        if msg.get("t") == "icon_request" and self.answer_icons:
            self._icons.append(msg)
        else:
            self.sent.append(msg)

    # --- The link ---

    def connect(self, connected=True):
        """The link to Knob Hub comes up (Knob OS says hello) or goes."""
        self.kp.kp_link_connected(1 if connected else 0)
        self.run(0.1)

    def send(self, msg, wait=0.0):
        """A message from Knob Hub (see knob-os/src/core/link.h)."""
        self.kp.kp_link_line(json.dumps(msg).encode())
        if wait:
            self.run(wait)

    def take(self, kind=None):
        """The messages sent since the last take (only those of type kind, if given)."""
        out = [m for m in self.sent if kind is None or m.get("t") == kind]
        self.sent = [m for m in self.sent if not (kind is None or m.get("t") == kind)]
        return out

    # --- Time and input ---

    def run(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            self.kp.kp_tick()
            while self._icons:
                req = self._icons.pop(0)
                self.send(test_icon(req["name"], req["size"]))
            time.sleep(0.004)

    def input(self, which, wait=0.5):
        self.kp.kp_input(which)
        self.run(wait)

    def turn_right(self, wait=0.5): self.input(RIGHT, wait)
    def turn_left(self, wait=0.5): self.input(LEFT, wait)
    def press(self, wait=0.6): self.input(PRESS, wait)
    def back(self, wait=0.6): self.input(BACK, wait)

    def tap(self, x, y, wait=0.6):
        self.kp.kp_touch(x, y, 1)
        self.run(0.12)
        self.kp.kp_touch(x, y, 0)
        self.run(wait)

    # --- What it shows and does ---

    def frame(self):
        """The screen: size x size pixels, BGRX."""
        self.run(0.08)
        return ctypes.string_at(self.kp.kp_frame(None), self.size * self.size * 4)

    def save_png(self, path, frame=None):
        raw = frame or self.frame()
        n = self.size
        rgb = bytearray(n * n * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
        rows = b"".join(b"\x00" + bytes(rgb[y * n * 3:(y + 1) * n * 3]) for y in range(n))

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 2, 0, 0, 0)) +
                    chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))

    def brightness(self):
        return self.kp.kp_brightness()

    def leds(self):
        """The LEDs' colours (0xRRGGBB) and the arc they're shown along: (colors, start, sweep)."""
        colors = (ctypes.c_uint32 * 16)()
        count, start, sweep = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
        self.kp.kp_leds(colors, 16, ctypes.byref(count), ctypes.byref(start), ctypes.byref(sweep))
        return [colors[i] for i in range(count.value)], start.value, sweep.value

    def vibrations(self):
        """The vibration effects played since the last call: "tick", "bump", "click", "alert"."""
        out = []
        while (e := self.kp.kp_vibration()) >= 0:
            out.append(EFFECTS[e])
        return out

    def motor(self):
        """How the motor on the knob feels: (positions, position, wrap, detent), or None before it was set."""
        v = [ctypes.c_int() for _ in range(4)]
        serial = self.kp.kp_motor(*[ctypes.byref(x) for x in v])
        if not serial:
            return None
        return v[0].value, v[1].value, bool(v[2].value), DETENTS[v[3].value]
