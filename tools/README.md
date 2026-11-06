# Tools

Scripts for checking changes without a knob, and for the README's screenshots. Run them from the repository root.
The Python ones need Python 3.10+ (and Pillow for `compare.py`, pyserial for the ones that talk to a real knob:
PlatformIO's own Python has it); run them with `python -I`. Those that use a real knob need Knob Hub closed, since it
holds the knob's port.

## Knob OS through the preview DLL

These load `knob_preview.dll` (build it with `pio run -e preview` in `knob-os`) and drive Knob OS directly: no window,
no Knob Hub, no simulator. `knob.py` is the shared helper: a `Knob` is a fresh Knob OS of a given board and screen size
that you send link messages to, turn, press and tap, and read the screen, brightness, LEDs, motor and vibrations of.

| Script | What it does |
|---|---|
| `checks.py` | Checks behaviour: LEDs and dial presses, pictures from another screen size, haptics, toasts, the screen timeout, cards, the Wi-Fi screen (also after Knob Hub reconnects), the busy spinner, keeping the screen awake, a push that isn't a tap, the picker, odd app data, scrolling text, settings changed while Knob Hub is away, the screen timeout's choices (also from an older Knob Hub). `--list` names them; pass names to run some. About five minutes, since the timeout waits for real. Exits with 1 on a failure. |
| `tour.py <dir>` | Saves every kind of screen as PNGs (`--board`, `--screen 240/360/480`). |
| `settings_shots.py [dir]` | Knob OS's own Settings screens as PNGs, into `design/screenshots/settings` by default. |
| `timer_shots.py <dir>` | The Timer app's screens for the apps repository's README, with Knob Hub's real icons (build `hub-icons` in Release first). |
| `compare.py <dir> --against <dir>` | Lists the screens that differ between two tours (exit 1 if any); `--sheet out.png` makes a contact sheet. |

Screens at 240 px must stay identical unless a change is meant to show: save a tour before the change, one after,
and compare them.

## The simulator and Knob Hub

| Script | What it does |
|---|---|
| `sim-capture.ps1` | Turns, presses or taps the running simulator with posted mouse messages (no focus change, no mouse) and captures its window (`-Out`). |
| `hub-shots.ps1 -OutDir docs\screenshots` | Knob Hub's pages at the README's size, with no knob connected (the paired knob is left out, so it can't connect over Wi-Fi), or with the simulator as `-Board <id>`. Backs up and restores Knob Hub's settings and the simulator's data, keeps the screen on for the shots, and puts a running Knob Hub back as it was. |
| `hub-page.ps1 -Page 0-3` | Shows a page of the open Knob Hub window (Knob, Apps, Board, Settings) through UI Automation. |
| `addapps-shot.ps1 -Out docs\screenshots\hub-add-apps.png` | Knob Hub's Add apps window at the README's size, then closes it. Knob Hub's window must be open. |
| `wifi-window.ps1 -Out <png>` | Opens Knob Hub's Wi-Fi window, waits for the knob's network list, captures it and closes it. Needs a knob connected; the picture shows nearby networks, so don't publish it as is. |
| `install-knob-os.ps1 [-ReadOnly]` | Settings > Knob OS through UI Automation: clicks Install, confirms, and prints the status until the knob is back. `-ReadOnly` only prints the card. |
| `installer-test.ps1 -Step install\|upgrade\|uninstall\|report [-Msi <msi>]` | Runs the installer one step at a time and reports what's on the PC afterwards (folder, Start menu, Run entry, the running Knob Hub, its log). Installing asks for administrator approval. |

## Knob Hub's code

| Tool | What it does |
|---|---|
| `dotnet run --project tools\hub-tests` | Tests Knob Hub's and the apps' code directly: the Timer app, the AudioDesk app with made-up channels (it never connects to AudioDesk), the screen timeout setting (old and new settings files), app ids, the Add apps catalog and the updater's checksum (both read GitHub). Build Knob Hub (Debug) first. Exits with the number of failures. |
| `tools\hub-icons` | Draws Knob Hub's built-in icons as the knob gets them (a line "name size" in, base64 A8 out), for `timer_shots.py`. |

A Knob Hub running in the tray connects to any simulator you start, so it sees what you do there.

## A real knob

| Script | What it does |
|---|---|
| `knob_listen.py <port> <seconds> <out.txt>` | Stands in for Knob Hub on USB and writes down everything the knob sends, with times. |
| `knob_boot_log.py <port> <text>` | Restarts the knob and prints the lines it writes while starting that contain the text. |
| `dotnet run --project tools\wifi-link-test [-- --wrong-key]` | Plays Knob Hub over Wi-Fi to the paired knob with Knob Hub's own encrypted link, and prints what the knob says; `--wrong-key` checks that the knob refuses another key. Close Knob Hub first. |
| `scroll_perf.py [port]` | Frame times for scrolling text: a list with short and with long labels, idle and while turning. Same needs as `knob_perf.py`. |
| `knob_perf.py [port]` | How fast the knob draws: turns through the Home ring over USB and prints the frame times (median and slowest), the CPU speed and the free internal RAM. Without a port it uses the plugged-in knob's (Knob OS's USB, 303A:0002). Needs a build with `-D KNOB_PERF` and Knob Hub closed; see the script's header. |
