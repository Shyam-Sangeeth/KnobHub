"""Knob OS's own Settings screens as PNGs, for design/screenshots/settings (240 px, CrowPanel 1.28"), through the
preview DLL.

    python -I tools/settings_shots.py [out_dir]     (default design/screenshots/settings)"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from knob import Knob  # noqa: E402
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("design", "screenshots", "settings")
os.makedirs(out, exist_ok=True)
k = Knob(board="crowpanel-1.28")
def png(name, wait=0.1):
    k.run(wait); k.save_png(os.path.join(out, name + ".png"))
k.connect()
k.send({"t": "apps", "items": [], "order": ["clock", "settings"]}, wait=0.4)
k.turn_right(); k.press(wait=0.8); png("settings")
k.press(); png("settings-display")
k.press(); png("settings-display-brightness")
k.back(); k.back()
k.turn_right(); k.press(); png("settings-lights")
k.press(); png("settings-lights-brightness")
k.back(); k.back()
k.turn_right(); k.press(); png("settings-connection")
k.connect(False); k.run(0.4); png("settings-connection-disconnected")
k.connect(True); k.run(0.4)
k.turn_right(); k.press(wait=0.3); png("toast-wifi-connected")
k.run(4)
k.back()
k.turn_right(); k.press(); png("settings-about")
k.turn_right(); k.turn_right(); k.press(wait=0.3); png("toast-restart")
