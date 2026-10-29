# Tools

Scripts for checking changes without a knob, and for the README's screenshots. Run them from the repository root.
The Python ones need Python 3.10+ (and Pillow for `compare.py`); run them with `python -I`.

## Knob OS through the preview DLL

These load `knob_preview.dll` (build it with `pio run -e preview` in `knob-os`) and drive Knob OS directly: no window,
no Knob Hub, no simulator. `knob.py` is the shared helper: a `Knob` is a fresh Knob OS of a given board and screen size
that you send link messages to, turn, press and tap, and read the screen, brightness, LEDs, motor and vibrations of.

| Script | What it does |
|---|---|
| `checks.py` | Checks behaviour: LEDs, pictures from another screen size, haptics, toasts, the screen timeout. `--list` names them; pass names to run some. About a minute, since the timeout waits for real. Exits with 1 on a failure. |
| `tour.py <dir>` | Saves every kind of screen as PNGs (`--board`, `--screen 240/360/480`). |
| `compare.py <dir> --against <dir>` | Lists the screens that differ between two tours (exit 1 if any); `--sheet out.png` makes a contact sheet. |

Screens at 240 px must stay identical unless a change is meant to show: save a tour before the change, one after,
and compare them.

## The simulator and Knob Hub

| Script | What it does |
|---|---|
| `sim-capture.ps1` | Turns, presses or taps the running simulator with posted mouse messages (no focus change, no mouse) and captures its window (`-Out`). |
| `hub-shots.ps1 -OutDir docs\screenshots` | Knob Hub's pages at the README's size, with no knob connected, or with the simulator as `-Board <id>`. Backs up and restores Knob Hub's settings and the simulator's data, keeps the screen on for the shots, and puts a running Knob Hub back as it was. |
| `hub-page.ps1 -Page 0-3` | Shows a page of the open Knob Hub window (Knob, Apps, Board, Settings) through UI Automation. |

A Knob Hub running in the tray connects to any simulator you start, so it sees what you do there.
