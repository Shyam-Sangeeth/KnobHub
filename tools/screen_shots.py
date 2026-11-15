"""The Screen app's screenshots (for the READMEs) through the preview DLL, with Knob Hub's real icons (hub-icons draws
them: `dotnet build tools/hub-icons -c Release` first). The data is what hub/Plugins/Screen/ScreenApp.cs sends.

    python -I tools/screen_shots.py <out_dir>"""
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from knob import Knob  # noqa: E402

out = sys.argv[1]
icons = subprocess.Popen([os.path.join(here, "hub-icons", "bin", "Release", "net10.0-windows10.0.19041.0", "hub-icons.exe")],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
k = Knob(board="crowpanel-1.28", screen=240)
k.answer_icons = False


def run(seconds):
    end = seconds
    while end > 0:
        k.run(0.1); end -= 0.1
        for req in k.take("icon_request"):
            icons.stdin.write(f"{req['name']} {req['size']}\n"); icons.stdin.flush()
            a8 = icons.stdout.readline().strip()
            if a8:
                px = req["size"]
                k.send({"t": "icon", "name": req["name"], "size": px, "w": px, "h": px, "hash": f"real-{req['name']}-{px}", "a8": a8})


def png(name, wait=0.6):
    run(wait)
    os.makedirs(out, exist_ok=True)
    k.save_png(os.path.join(out, name + ".png"))


k.connect()
k.send({"t": "settings", "brightness": 100, "timeout_seconds": 0, "dim_idle": False})
k.send({"t": "apps", "items": [
    {"id": "media", "name": "Media", "subtitle": "Now playing", "icon": "play", "color": "#4ADE80"},
    {"id": "screen", "name": "Screen", "subtitle": "Off and on", "icon": "power", "color": "#60A5FA"}],
    "order": ["clock", "media", "screen", "settings"]})
run(0.5)
k.turn_right(); k.turn_right()
png("home-screen", 1.5)
k.press(); run(0.5)
on = {"label": "SCREENS", "text": "ON", "value": 100, "color": "#60A5FA", "busy": False}
k.send({"t": "screen", "app": "screen", "screen": "screen", "template": "ring", "data": on})
png("screen-on")
k.send({"t": "update", "app": "screen", "screen": "screen", "data": {"busy": True}})
png("screen-switching")
k.send({"t": "update", "app": "screen", "screen": "screen",
        "data": {"label": "SCREENS", "text": "OFF", "value": 0, "color": "#5B6472", "busy": False}})
png("screen-off")
icons.stdin.close()
