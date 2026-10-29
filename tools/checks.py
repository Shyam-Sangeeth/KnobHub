"""Checks Knob OS's behaviour through the preview DLL: what the knob does, not how it looks (tour.py is for that).

    python -I tools/checks.py              all of them (about a minute: the screen timeout waits for real)
    python -I tools/checks.py leds idle    some of them
    python -I tools/checks.py --list

Exits with 1 if any check fails. Build the DLL first (`pio run -e preview`)."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from knob import Knob  # noqa: E402

APPS = {"t": "apps", "items": [{"id": "demo", "name": "AudioDesk", "subtitle": "", "icon": "audiodesk", "color": "#8B7CF6"}],
        "order": ["demo", "clock", "settings"]}


def expect(what, got, wanted):
    if got != wanted:
        raise AssertionError(f"{what}: got {got!r}, wanted {wanted!r}")


def dial(value, color="#4CC2FF"):
    return {"t": "screen", "app": "demo", "screen": "v", "template": "dial",
            "data": {"label": "VOLUME", "icon": "music", "color": color, "value": value}}


def check_leds():
    """Level meter fills the LEDs in order; App colour lights them all; Off; one LED gets brighter; the arc."""
    k = Knob(board="crowpanel-1.28")
    k.connect()
    k.send(APPS, wait=0.3)
    colors, start, sweep = k.leds()
    expect("Home (the clock page): all lit, the default arc", (start, sweep), (270, 360))
    expect("Home: 5 LEDs", len(colors), 5)
    k.turn_left()
    expect("Home on AudioDesk: its colour at 70%", k.leds()[0], [0x6156AC] * 5)
    k.press(wait=0.8)
    k.send(dial(64), wait=0.2)
    expect("dial at 64: 3 full, 1 a fifth, 1 off", k.leds(), ([0x3587B2] * 3 + [0x0A1B23, 0], 135, 270))
    k.send(dial(0), wait=0.2)
    expect("dial at 0: all off", k.leds()[0], [0] * 5)
    k.send({"t": "settings", "led_mode": 1, "led_brightness": 100})
    k.send(dial(64), wait=0.2)
    expect("App colour: all in the colour", k.leds()[0], [0x4CC2FF] * 5)
    k.send({"t": "settings", "led_mode": 2}, wait=0.2)
    expect("Off", k.leds()[0], [0] * 5)

    one = Knob(board="crowpanel-1.46")
    one.connect()
    one.send(APPS, wait=0.3)
    one.turn_left()
    one.press(wait=0.8)
    one.send(dial(64), wait=0.2)
    expect("one LED at 64: brighter by the level", one.leds()[0], [0x215570])


def check_pictures():
    """Album art drawn for a 240 or 360 px knob fills the same space on the 480 px preview; too big falls back."""
    import base64
    import struct

    def art(px, screen):
        out = bytearray()
        for y in range(px):
            for x in range(px):
                r, g, b = [(230, 60, 60), (60, 200, 90), (70, 110, 240), (240, 190, 40)][(x >= px // 2) + 2 * (y >= px // 2)]
                out += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        return {"w": px, "h": px, "rgb565": base64.b64encode(bytes(out)).decode(), "screen": screen}

    k = Knob(board="crowpanel-1.46", screen=480)
    k.connect()
    k.send({"t": "apps", "items": [{"id": "media", "name": "Media", "icon": "play", "color": "#4ADE80"}],
            "order": ["media", "clock", "settings"]}, wait=0.3)
    k.turn_left()
    k.press(wait=0.8)
    base = {"title": "x", "artist": "M83", "playing": False, "position": 60, "duration": 200}

    def show(picture):
        data = dict(base, art=picture) if picture else base
        k.send({"t": "screen", "app": "media", "screen": "now", "template": "media", "data": data}, wait=0.5)
        return k.frame()

    def strongly_differ(a, b):
        return sum(1 for i in range(0, len(a), 4) if max(abs(a[i + c] - b[i + c]) for c in range(3)) > 64)

    none, for240, for360, too_big = show(None), show(art(80, 240)), show(art(120, 360)), show(art(150, 360))
    if strongly_differ(none, for240) < 10000:
        raise AssertionError("art drawn for 240 px doesn't show")
    if strongly_differ(for240, for360) > 2000:
        raise AssertionError("art drawn for 360 px doesn't match the same art drawn for 240 px")
    expect("art too big even scaled: the icon instead", strongly_differ(none, too_big), 0)


def check_idle():
    """Dims halfway to the timeout, then turns off; an input while off only wakes it; a tap too; following a knob."""
    k = Knob(board="crowpanel-1.28")
    k.connect()
    k.send({"t": "settings", "brightness": 80, "timeout": 0, "dim_idle": True, "leds_idle_off": True, "led_mode": 1})
    k.send(APPS, wait=0.3)
    k.turn_left()
    k.take()
    k.run(7.6)
    expect("7.5 s idle: dimmed to a quarter", k.brightness(), 20)
    expect("dimmed: LEDs off (When dimmed off)", k.leds()[0], [0] * 5)
    expect("reported", [m["state"] for m in k.take("idle")], ["dim"])
    k.turn_right()
    expect("a turn while dimmed: acts", [m["input"] for m in k.take("key")], ["right"])
    expect("and brightens", k.brightness(), 80)
    k.turn_left()
    k.take()
    k.run(15.3)
    expect("15 s idle: off", k.brightness(), 0)
    expect("off: LEDs off", k.leds()[0], [0] * 5)
    before = k.frame()
    k.press()
    expect("a press while off: no key", k.take("key"), [])
    expect("only wakes", (k.brightness(), k.frame() == before), (80, True))
    k.press()
    expect("the next press opens the app", [m["app"] for m in k.take("open")], ["demo"])
    k.send({"t": "close", "app": "demo"}, wait=0.6)
    k.take()
    k.run(15.3)
    before = k.frame()
    k.tap(120, 100)
    expect("a tap while off only wakes", (k.brightness(), k.frame() == before), (80, True))
    k.send({"t": "idle", "state": "off", "follow": True}, wait=0.3)
    expect("following a knob that's off", k.brightness(), 0)
    k.turn_right()
    expect("a replayed turn while following acts", [m["input"] for m in k.take("key")], ["right"])
    k.send({"t": "idle", "follow": False}, wait=0.3)
    expect("timing its own again: awake", k.brightness(), 80)


def check_toasts():
    """A toast wakes the screen; an app's screen update doesn't; LEDs stay on when dimmed with When dimmed on."""
    k = Knob(board="crowpanel-1.28")
    k.connect()
    k.send({"t": "settings", "brightness": 80, "timeout": 0, "dim_idle": True, "leds_idle_off": False, "led_mode": 1})
    k.send(APPS, wait=0.3)
    k.turn_left()
    k.run(15.3)
    expect("off", k.brightness(), 0)
    expect("off: LEDs off even with When dimmed on", k.leds()[0], [0] * 5)
    k.send({"t": "update", "app": "demo", "screen": "x", "data": {}}, wait=0.4)
    expect("an app update while off: stays off", k.brightness(), 0)
    k.send({"t": "toast", "text": "Sonar is off"}, wait=0.4)
    expect("a toast wakes it", k.brightness(), 80)
    k.run(7.6)
    expect("dimmed again", k.brightness(), 20)
    expect("dimmed: LEDs on (When dimmed on)", k.leds()[0], [0x6156AC] * 5)


def check_haptics():
    """The motor knob's feel per screen, its vibration effects; a board without either does nothing."""
    k = Knob(board="motor-knob")
    k.connect()
    k.send({"t": "apps", "items": [{"id": "demo", "name": "Demo", "icon": "x", "color": "#8B7CF6"}],
            "order": ["clock", "demo", "settings"]}, wait=0.3)
    expect("Home: a firm click per page, round and round", k.motor(), (3, 0, True, "firm"))
    k.turn_right()
    expect("a turn on a motor knob: no vibration (the motor clicks)", k.vibrations(), [])
    k.press()
    expect("a press", k.vibrations(), ["click"])
    k.send({"t": "screen", "app": "demo", "screen": "l", "template": "list",
            "data": {"title": "LIST", "items": [{"id": "a", "label": "A"}, {"id": "b", "label": "B"}, {"id": "c", "label": "C"}]}}, wait=0.3)
    expect("a list: a click per row, end stops", k.motor(), (3, 0, False, "medium"))
    for _ in range(3):
        k.turn_right(wait=0.06)
    expect("turned past the end", k.motor(), (3, 2, False, "medium"))
    k.send({"t": "screen", "app": "demo", "screen": "d", "template": "dial", "data": {"label": "VOLUME", "value": 96}}, wait=0.3)
    expect("a dial: fine clicks 0-100", k.motor(), (51, 48, False, "fine"))
    for _ in range(4):
        k.turn_right(wait=0.06)
    expect("held at 100", k.motor(), (51, 50, False, "fine"))
    k.send({"t": "update", "app": "demo", "screen": "d", "data": {"value": 40}}, wait=0.3)
    expect("the app set 40: the motor follows", k.motor(), (51, 20, False, "fine"))
    k.vibrations()
    k.send({"t": "toast", "text": "Hi"}, wait=0.1)
    expect("a toast", k.vibrations(), ["alert"])
    k.send({"t": "settings", "vibration": False})
    k.send({"t": "toast", "text": "Hi again"}, wait=0.1)
    expect("vibration off", k.vibrations(), [])

    plain = Knob(board="crowpanel-1.46")
    plain.connect()
    plain.turn_right()
    plain.press()
    expect("no motor, no vibration motor", (plain.motor(), plain.vibrations()), (None, []))


CHECKS = {"leds": check_leds, "pictures": check_pictures, "haptics": check_haptics, "toasts": check_toasts,
          "idle": check_idle}

if __name__ == "__main__":
    if "--list" in sys.argv:
        for name, fn in CHECKS.items():
            print(f"{name:9} {fn.__doc__}")
        sys.exit(0)
    names = [a for a in sys.argv[1:] if not a.startswith("-")] or list(CHECKS)
    failed = 0
    for name in names:
        started = time.time()
        try:
            CHECKS[name]()
            print(f"pass  {name} ({time.time() - started:.0f} s)")
        except AssertionError as e:
            failed += 1
            print(f"FAIL  {name}: {e}")
    sys.exit(1 if failed else 0)
