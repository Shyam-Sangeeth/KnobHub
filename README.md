# Knob Hub

A physical controller for the PC built on the Elecrow CrowPanel 1.28" ESP32-S3 rotary display (round 240x240 touch screen inside a turnable, pressable knob, 5 RGB LEDs, Wi-Fi). The knob is a small launcher: its home screen is a ring of pages (Clock, the installed apps, Settings) turned with the knob. Apps run on the PC and talk to the knob through "Knob Hub", a small tray app; AudioDesk (https://github.com/Shyam-Sangeeth/AudioDesk) is the first app.

## How it works

- The knob runs no apps. Knob Hub sends it the list of apps and then screens to show: a template name plus data (dial, tiles, list, controls, ring, media). The knob draws them and reports what was done on them (press, rotate, tap, select, back).
- Each app is a Knob Hub plugin on the PC that does the work, e.g. the AudioDesk plugin turns knob input into calls to AudioDesk's local API.
- Icons work the same way: the knob asks Knob Hub for an icon by name at the size it shows it, and Knob Hub draws it (from the app's own icons or its built-in set) and sends it. Knob OS only has font symbols for its own Clock and Settings.
- So a new app, or a change to an app, needs no firmware update, as long as the templates fit it.
- The knob keeps the app list and the icons Knob Hub sent, so its Home ring is complete before Knob Hub connects (opening an app still needs Knob Hub). Every time Knob Hub connects, the knob checks for changes: a new or changed app list replaces the kept one, icons that Knob Hub now draws differently (an updated app) are fetched again, and the icons of removed or turned-off apps are deleted.

## Repository layout

- `knob-os/`: Knob OS firmware (PlatformIO, LVGL 9.2, C++17).
  - `src/core/`: shared UI and logic.
  - `src/hal/sim/`: simulator (SDL2 window).
  - `src/hal/preview/`: `knob_preview.dll` used by Knob Hub's live preview.
  - `src/hal/esp32/`: the real knob (stub until the board arrives).
  - `fonts/make_fonts.py`: builds `src/core/fonts/knob_font_*.c` (Montserrat Medium + LVGL symbols + Latin-1, Latin Extended-A and typographic punctuation) with `npx lv_font_conv@1.5.3`; needs Node.js. It also builds digits-only fonts (`knob_font_digits_36/60/72/96`) for big numbers.
- `hub/`: Knob Hub, WPF .NET 10 tray app. `hub/KnobHub.slnx` contains `KnobHub.Sdk` (class library for apps) and `KnobHub` (the app). The solution now also contains the AudioDesk app.
  - `hub/Plugins/AudioDesk/`: the AudioDesk app for the knob (a plugin).
    - `hub/Plugins/AudioDesk/Icons/` holds the AudioDesk app's own icons (`make_icons.ps1` draws the game controller and aux cable PNGs; `sonar.png` is Sonar's logo).
  - `hub/Plugins/Counter/`: the Counter app (a plugin), a small example app.

## Building and running

### Simulator

Prerequisites:
- PlatformIO
- SDL2 MinGW development package unpacked to `%USERPROFILE%\.tools\SDL2\SDL2-2.30.9` (from https://github.com/libsdl-org/SDL/releases, file `SDL2-devel-2.30.9-mingw.tar.gz`)
- 64-bit MinGW gcc in `%USERPROFILE%\.mingw64\bin`

In `knob-os`, run:

```bash
pio run -e sim -t exec
```

Build output goes to `%USERPROFILE%\.cache\knob-os`.

### Knob Hub

In `hub`, run:

```bash
dotnet build KnobHub\KnobHub.csproj
KnobHub\bin\Debug\net10.0-windows10.0.19041.0\KnobHub.exe
```

Building Knob Hub also builds `knob_preview.dll` (`pio run -e preview`) if it is missing and copies it next to the app.

Knob Hub connects to the simulator on 127.0.0.1:5718 and retries every 2 seconds. With no knob connected, the preview itself works as a virtual knob. USB serial and Wi-Fi connections are not written yet.

## Controls

### Simulator

| Control | Action |
| --- | --- |
| Mouse wheel | Turn |
| Middle click | Press (held half a second = back, the knob's long press) |
| Left mouse button | Touch (tap, swipe) |
| Swipe right | Back (any screen but Home) |

### Knob Hub live preview

| Control | Action |
| --- | --- |
| Scroll | Turn |
| Middle click | Press, hold = back |
| Left button | Touch (tap, drag, swipe) |
| Swipe right | Back (any screen but Home) |

While a knob (the simulator) is connected the preview mirrors it; what you do in the preview is done on the knob.

### Gestures on the knob

- Swipe left/right on Home turns pages.
- Swipe right on any other screen goes back.
- The knob's long press goes back.

## Apps

### Built-in apps

- Media: shows what is playing in Windows (album art, title and artist, play/pause, previous/next, turning the knob seeks 5 seconds per step). Titles in scripts the knob's fonts don't have (Malayalam, Hindi, Japanese...) are drawn by Knob Hub with Windows' fonts and sent as pictures.

### Apps page

The Apps page in Knob Hub lists everything on the Home ring in order, Clock and Settings included: drag a row to move it (Home starts on the first one), turn apps on or off, change their settings, and install plugins.

### Plugins

A plugin is a folder with `app.json` and a .NET assembly, zipped and renamed to `.knobapp`. It is installed from the Apps page into `%LOCALAPPDATA%\KnobHub\Apps\<id>`.

`app.json` fields: `id`, `name`, `subtitle`, `description`, `icon`, `color`, `version`, `author`, `assembly` (file name of the DLL), `type` (full name of the KnobApp subclass).

The class derives from `KnobHub.Sdk.KnobApp` (override `Open` to show the first screen, `Input` to react, optional `Start`/`Stop`/`Closed`) and talks to the knob through `IKnobHost` (`Show`, `Update`, `Close`, `Toast`, `Log`).

Apps can have settings. Override `Settings` to list them (`AppSetting(id, label, options, default)`: a drop-down of options). Knob Hub shows them on the app's row on the Apps page and keeps the choice in settings.json. Read the chosen option's index with `Host.Setting(id)`; `SettingChanged(id)` is called when it changes.

Screens are `DialScreen` (one big value), `TilesScreen` (up to three round tiles with pills), `ListScreen` (rows), `ControlsScreen` (a value with a toggle and a picker, e.g. an audio channel's volume, a feature on/off and the output device), `RingScreen` (a number in the middle of a full ring, drawn as big as fits; e.g. a counter or a timer) and `MediaScreen` (now playing).

Icons are names. Knob Hub has common ones built in (music, play, pause, game, headphones, speaker, volume, mic, tv, settings, clock, lights, wifi, star, heart and more). An app adds its own by overriding `Icons` (name to `KnobIcon.Glyph(text, font)`, `KnobIcon.Image(pngBytes)` or `KnobIcon.Path(svgPathData)`); Knob Hub draws them at the size the knob asks for. Names are shared between apps, so prefix your own (e.g. `myapp.logo`).

Plugins run with Knob Hub's permissions: only install apps you trust.

### AudioDesk app

- Opening it shows the Game, Media and Aux channels. Each is a circle: a thin ring in the channel's colour, filled by a thicker arc as far as the volume (empty when muted), with the output device it plays on inside (Sonar's output while the channel goes through Sonar; long names scroll) and a small Sonar badge at the bottom of the ring while it goes through Sonar. A muted channel's icon is crossed out, and an arc spins round the ring while a channel switches device; nothing is written under the circles. Below are a Sonar pill (when SteelSeries Sonar is installed) and Headset | Home Theater.
- Pressing a channel opens its controls: the volume (the ring, with the % at the bottom, or "Muted"), the channel's own Sonar on/off, and its output device (name and format on a card; while the channel goes through Sonar, the devices Sonar can play it on). The knob's press moves between them and turning changes the selected one: volume (each click changes the volume by the "Volume step" set on the AudioDesk row of Knob Hub's Apps page: 1%, 2% (the default), 5% or 10%), Sonar on/off, or the next/previous device (it switches once you stop turning). Tapping the icon mutes; tapping the volume % or pressing the knob twice quickly mutes too. Long press or swipe right goes back.
- Volume follows AudioDesk: while a channel goes through Sonar it is Sonar's own volume for that channel, otherwise each app's volume in the Windows mixer. Changes made in AudioDesk show up on the knob and the other way round.
- It talks to AudioDesk (https://github.com/Shyam-Sangeeth/AudioDesk) through AudioDesk's local API, so AudioDesk must be running (a version with the local API, newer than 1.0.4). If it isn't running, the knob says so, with a "Start AudioDesk" button when it can find AudioDesk on the PC (its Start with Windows entry or the installer's folder).
- If AudioDesk stops (or restarts), the knob shows that it isn't running and goes back to the same screen once AudioDesk is running again.
- Building `hub/Plugins/AudioDesk` (`dotnet build -c Release`) produces `bin\Release\AudioDesk.knobapp`; install it from Knob Hub's Apps page with "Install app...".

### Counter app

A small example app (`hub/Plugins/Counter`). It opens at 0 and counts up by one for each click turned to the right and down by one for each click to the left, never below 0. The number sits in the middle of a ring and gets smaller as it grows so it always fits. The ring fills once every 20 counts; each time it's full, the next lap fills in a new random bright colour over the last one. Build it with `dotnet build -c Release` and install `bin\Release\Counter.knobapp` from the Apps page.

## Link protocol

The PC and knob communicate over newline-delimited JSON, one message per line. All messages are documented in `knob-os/src/core/link.h`.

## Files

- Settings: `%LOCALAPPDATA%\KnobHub\settings.json`
- Log: `%LOCALAPPDATA%\KnobHub\knobhub.log` (connections, apps opened, app problems)
- The simulator's kept data (its stand-in for the knob's flash): `%LOCALAPPDATA%\KnobHub\Simulator\` (`apps.json`, `icons.bin`). The real knob keeps the same files in its flash.

## Connection

USB serial and Wi-Fi connections are not written yet. The planned design:
- First time USB only (pairs the knob with Knob Hub).
- USB always has priority when plugged in.
- Otherwise Wi-Fi once set up.
- Wi-Fi is set up from Settings > Connection > Wi-Fi setup (pairing mode: the knob opens its own network and a setup page at knobhub.local, which switch off again once it's connected).

## Status

The simulator, Knob Hub with live preview, the Media app and the knob's Settings work. The AudioDesk app now works too; next are USB/Wi-Fi connection and the real board.
