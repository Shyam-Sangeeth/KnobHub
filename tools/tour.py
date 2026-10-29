"""Shows every kind of Knob OS screen with fixed data and saves each as a PNG, to compare Knob OS before and after a
change (with compare.py) or between boards and screen sizes.

    python -I tools/tour.py <out_dir> [--board crowpanel-1.46] [--screen 240|360|480]

At 240 px, screens must stay identical pixel for pixel unless a change is meant to show: save a tour before the
change, one after, and compare them."""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from knob import Knob  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("out_dir")
parser.add_argument("--board")
parser.add_argument("--screen", type=int, default=240)
args = parser.parse_args()
os.makedirs(args.out_dir, exist_ok=True)
k = Knob(board=args.board, screen=args.screen)


def png(name):
    k.run(0.08)
    k.save_png(os.path.join(args.out_dir, name + ".png"))


def screen(template, data, app="demo", name="s"):
    k.send({"t": "screen", "app": app, "screen": name, "template": template, "data": data}, wait=0.45)


k.connect()
k.send({"t": "apps", "items": [
    {"id": "demo", "name": "AudioDesk", "subtitle": "3 channels", "icon": "audiodesk", "color": "#8B7CF6"},
    {"id": "media", "name": "Media", "subtitle": "Now playing", "icon": "play", "color": "#4ADE80"}],
    "order": ["demo", "media", "clock", "settings"]}, wait=0.5)
k.turn_left()  # Home starts on the clock: back to Media, then AudioDesk
png("01-home-media")
k.turn_left()
png("02-home-app")
k.press(wait=0.8)  # open AudioDesk

screen("tiles", {"title": "AUDIODESK", "selected": "game", "items": [
    {"id": "game", "label": "Game", "icon": "game", "color": "#4CC2FF", "value": 80, "detail": "Headset Earphone"},
    {"id": "media", "label": "Media", "icon": "media", "color": "#FF9F43", "value": 60, "detail": "Digital Output", "tag_icon": "sonar"},
    {"id": "aux", "label": "Aux", "icon": "aux", "color": "#C084FC", "value": 0, "detail": "Headset Earphone", "muted": True}],
    "pills": [{"id": "sonar", "kind": "toggle", "label": "Sonar", "on": True, "icon": "sonar"},
              {"id": "output", "kind": "segment", "options": ["Headset", "Home Theater"], "selected": 0}]})
png("03-tiles")
screen("controls", {"label": "MEDIA", "icon": "music", "color": "#FF9F43", "value": 72, "muted": False,
    "toggle": {"id": "sonar", "label": "Sonar", "icon": "sonar", "on": True, "on_color": "#4ADE80", "off_color": "#F87171"},
    "picker": {"selected": "gamedac", "options": [
        {"id": "gamedac", "name": "Headset Earphone (GameDAC Hi-Res)", "detail": "Sonar - 24-bit, 96 kHz"},
        {"id": "spdif", "name": "Digital Output", "detail": "16-bit, 48 kHz"}]}})
png("04-controls")
screen("dial", {"label": "VOLUME", "icon": "music", "color": "#4CC2FF", "value": 64, "unit": "%", "level": 0.5,
                "button": {"id": "device", "label": "Headset"}, "hint": "Press to mute"})
png("05-dial")
screen("list", {"title": "DISPLAY", "note": "Changes apply now", "hint": "Press to change", "items": [
    {"id": "b", "label": "Brightness", "value": "80%", "icon": "sun"}, {"id": "t", "label": "Screen timeout", "value": "30 s"},
    {"id": "c", "label": "Clock", "detail": "12-hour"}, {"id": "d", "label": "Dim", "danger": True}]})
png("06-list")
screen("list", {"title": "AUDIODESK", "note": "AudioDesk isn't running", "hint": "Open it on your PC"})
png("07-list-empty")
screen("ring", {"text": "512", "value": 60, "color": "#A3E635", "track_color": "#F472B6"})
png("08-ring")
screen("media", {"title": "Midnight City", "artist": "M83", "source": "Spotify", "playing": True,
                 "position": 60, "duration": 200, "can_seek": True}, app="demo", name="now")
png("09-media")
k.send({"t": "toast", "text": "Sonar is off"}, wait=0.15)
png("10-toast")
k.run(3)
k.send({"t": "close", "app": "demo"}, wait=0.6)  # the app closes: back to Home (its page)
# Knob OS's own Settings: the page before AudioDesk (the ring wraps), then open it.
k.turn_left()
png("11-home-settings")
k.press(wait=0.7)
png("12-settings")
k.press()
png("13-settings-display")
k.connect(False)
k.run(0.3)
png("14-settings-disconnected")
print("saved 14 screens in", args.out_dir)
