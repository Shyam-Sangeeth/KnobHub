# Knob Hub

A PC controller on a round ESP32-S3 knob display: the Elecrow CrowPanel 1.28" (240 x 240) or 1.46" (360 x 360)
rotary displays, and later the user's own knob with a motor (SmartKnob style). Screens are always round and boards
always ESP32s. Two halves:

- **`knob-os/`**: Knob OS, the firmware (PlatformIO, LVGL 9.2.2, ArduinoJson 7, C++17). A launcher that draws generic
  screens.
- **`hub/`**: Knob Hub, a WPF tray app (.NET 10, C#). It runs the apps (plugins) and sends the knob what to draw.

The README is the user and developer documentation; keep it in step with the code.

## The one rule: Knob OS stays app-agnostic

Knob OS must never know about a specific app. Apps are Knob Hub plugins that send screens as **template name +
data**:

| Template | File |
|---|---|
| dial | `tpl_dial.cpp` |
| tiles | `tpl_tiles.cpp` |
| list | `tpl_list.cpp` |
| controls | `tpl_controls.cpp` |
| ring | `tpl_ring.cpp` |
| media | `tpl_media.cpp` |

Templates are registered in `templates.cpp`. Colours, units, texts and icons all come in the data. The knob reports
input (rotate, press, tap, select, back).

When an app needs something new, add a generic option to a template, or a new template, plus its SDK screen type in
`hub/KnobHub.Sdk/Screens.cs`. Never write app code in the firmware: an app change must never need a firmware update.

## Several boards

`knob-os/src/core/boards.cpp` lists the boards (mirrored in `hub/KnobHub/Core/KnobBoard.cs`): screen size, touch,
LED count, vibration motor, motor on the knob. A knob reports its board in `hello`; Knob Hub shows only the settings
that board has and makes its preview the same board, but always at 480 px, one pixel per screen pixel (the user's
choice: one preview size, only the settings and LEDs follow the board). `kp_screen(240 or 360)` renders as a real knob
for pixel tests.

- **Screens are designed at 240 px and scaled.** Every length in screen code goes through `ui::px()`, fonts come from
  `ui::font(design size)` / `ui::digits()`, and `ui::size()` is the diameter. Lengths measured at run time (label
  widths, font metrics) are real pixels already: don't scale them. At 240 nothing may change, pixel for pixel.
- **Fonts per size:** `knob-os/fonts/make_fonts.py` generates each diameter's set (`DIAMETERS`: the boards' 240 and
  360, and 480 for the preview) and `fonts.cpp`. A new board size needs adding there.
- **A new board:** an entry in `boards.cpp` and `KnobBoard.cs`, its fonts, a pin file in `src/hal/esp32/boards/`, and
  a `platformio.ini` env setting `KNOB_BOARD` and `KNOB_FONT_SET`.
- **Haptics** (`haptics.h`): each screen's `feel()` says how turning should feel (detent positions, end stops, wrap,
  strength), derived from what it shows; Knob OS passes it to `hal_motor_feel` and plays `hal_vibrate` effects (tick,
  bump at an end stop, click, alert). Apps never deal with it.
- **Lights** (`lights.h`): each screen's `glow()` gives a colour, an optional level (0-100) and where its progress
  arc is (start, sweep); Knob OS turns it into LED colours by the Lights settings (Level meter, App colour, Off) and
  passes them to `hal_set_leds`. A screen with a new kind of arc returns its own start and sweep. The preview shows
  them as a glow (`ShowLeds`): one LED a halo, several along the arc, one part per LED.
- **Screen timeout** (`idle.h`): the time since the last input (LVGL's inactive time; `idle::input()` counts knob
  turns and presses) dims and then turns off the screen. An input while it's off only wakes it: knob inputs are dropped
  in `knob_os_input`, touches by a layer over everything until the finger lifts. A toast wakes it (`idle::wake`); app
  screen updates don't, or the Media app's position updates would keep it on. The knob reports its state in an `idle`
  message, and Knob Hub's preview follows it while mirroring (`follow`), so a wake-up input never acts on the preview
  either.
- **Pictures** (album art, `TextPicture`) carry `screen`, the size Knob Hub drew them for (`KnobSession` adds it);
  Knob OS scales one drawn for another size (`ui::picture_px`), so the 480 px preview can mirror any knob. Touches
  are rescaled between the two in `KnobSession`.

## Commands

```bash
# knob-os (build output goes to %USERPROFILE%\.cache\knob-os, outside OneDrive)
pio run -e sim -t exec      # simulator window (SDL2); Knob Hub connects to it on 127.0.0.1:5718
                            # program.exe --board crowpanel-1.46 (or motor-knob) for another board
pio run -e preview          # knob_preview.dll for Knob Hub's live preview
pio run -e crowpanel-128    # a real board, one env per board (hardware layer src/hal/esp32 still mostly stubs)
pio run -e crowpanel-146

# hub
dotnet build KnobHub\KnobHub.csproj          # also builds/copies knob_preview.dll if missing
KnobHub\bin\Debug\net10.0-windows10.0.19041.0\KnobHub.exe
dotnet build Plugins\Counter -c Release       # -> bin\Release\Counter.knobapp
powershell -ExecutionPolicy Bypass -File Plugins\pack-release.ps1 [-NoPublish]   # publish apps to KnobHub-Apps
```

Editing `platformio.ini` wipes PlatformIO's build folders, so rebuild both `sim` and `preview` afterwards.

There are no unit tests. Check changes in the **simulator** and in Knob Hub's **live preview** (a virtual knob when no
simulator is connected), and keep `design/screenshots/` current when screens change.

- **Pixel checks:** drive `knob_preview.dll` from a script (ctypes): `kp_screen(240)` or `kp_screen(360)`, `kp_board`,
  `kp_start`, then send link messages and read `kp_frame`. Screens at 240 must stay identical pixel for pixel unless
  the change is meant to show.
- **Without the user's mouse or focus:** post `WM_MOUSEWHEEL` (turn) and `WM_MBUTTONDOWN`/`UP` (press) to the
  simulator's window (it can stay minimized), and capture Knob Hub with `PrintWindow`. Knob OS's own Settings >
  Display > Brightness is a dial that's safe to open. Don't open apps that act on the PC (AudioDesk, Media) to test.

## Layout

**knob-os/src/core**
- `knob_os.cpp`: startup.
- `home.cpp`: the Home ring.
- `apps.cpp` and `icons.cpp`: the cached app list (`apps.json`) and icon cache (`icons.bin`, format "KIC1").
- `link.cpp/.h`: the protocol. Every message is documented in `link.h`, in the `hub_link` namespace (not `link`,
  which clashes with newlib's `link()`).
- `settings.cpp`: the knob's settings, kept in `settings.json`. Knob Hub's saved settings win: it sends them after
  every hello, and the knob only sends a setting when it's changed on the knob.
- `message.cpp`, `toast.cpp`.
- `ui.cpp/.h`: shared widgets, the scaling helpers (`ui::px`, `ui::font`, `ui::size`, `ui::picture_px`), and the ink
  helpers `ui::ink_top` and `ui::ink_height` used to centre text visually.
- `text_picture.cpp/.h`: text Knob Hub drew as a picture (scripts the fonts lack), scrolling when long.
- `board.h`/`boards.cpp`: the known boards. `haptics.cpp/.h`: what the knob feels like. `lights.cpp/.h`: what the
  LEDs show. `idle.cpp/.h`: the screen timeout and dimming, and the screen's brightness.
- `hal.h`: everything Knob OS needs from a hardware layer.
- `theme.h`: colours.

**knob-os/src/hal**
- `sim`: SDL, mouse only, fixed-size window; `--board <id>`; prints what the motors and LEDs would do.
- `preview`: the DLL, with in-memory storage, 480 px unless `kp_screen` says otherwise; `kp_board` before
  `kp_start`; `kp_vibration`/`kp_motor` report haptics and `kp_leds` the LEDs.
- `esp32`: LittleFS; one pin file per board in `boards/`.

**hub/KnobHub**
- `Core/KnobSession.cs`: talks to one knob (apps, screens, icons).
- `Core/KnobConnection.cs` (simulator TCP) and `Core/KnobPreview.cs` (the DLL, always 480 px; it loads a fresh copy
  from %TEMP% to become another board, since Knob OS starts only once per DLL). While it isn't on show (`Shown`: the
  window in the tray, minimized or on another page) it ticks 4 times a second and copies no frames.
- `Core/KnobBoard.cs`: the boards, and the board in a hello.
- `Core/KnobIcons.cs`: draws A8 icons.
- `Core/AppCatalog.cs`: the GitHub app list.
- `Core/HubSettings.cs`: `%LOCALAPPDATA%\KnobHub\settings.json`.
- `Apps/AppManager.cs` (install, order, settings, `.knobapp`), `Apps/AppHost.cs`, and `Apps/MediaApp.cs` (built in).
- `MainWindow`: the Knob, Apps, Board and Settings pages, with its own title bar via `WindowChrome`. Board has the
  board's details and the settings only some boards have (Lights, Haptics). There's no board picker (the user's
  choice): the preview is always the last connected knob's board; try others with the simulator's `--board`. The
  preview's LEDs glow round it.
- `AddAppsWindow`: the modal Add apps window.

**hub/KnobHub.Sdk**
- `KnobApp`, `IKnobHost`, `AppSetting`, `KnobIcon`, and the screen types.

**hub/Plugins**
- `AudioDesk` talks to AudioDesk's local API (named pipe `AudioDesk.Api`, AudioDesk repo `LocalApi.cs`). Keep both
  sides in step.
- `Counter` is the small example app.

## Things that bite

- **Plugins load from memory** (`PluginLoadContext.LoadFromMemory` in `AppManager.cs`), so their files are never
  locked. Loading them from the file path again breaks updating and uninstalling apps while Knob Hub runs.
- **Icons are cached on the knob by hash.** If you change how `KnobIcons` draws, bump `DrawVersion` in
  `KnobIcons.cs` so knobs fetch icons again.
- **Fonts:** `knob-os/fonts/make_fonts.py` generates them (Montserrat Medium + Latin ranges + LVGL symbols, plus
  digits-only big fonts). A character outside them draws as a box. Knob Hub sends text in other scripts as pictures
  (`TextPicture`).
- **Line breaks:** `LV_TXT_BREAK_CHARS` deliberately has no hyphen, so "Wi-Fi" never breaks.
- **Centring:** centre visually, using ink metrics, not the line box. The selection outline is 2 px, drawn tight to
  the control (no padding).
- **The ESP32 build needs `-D LV_KCONFIG_IGNORE`** (already set).
- **Scaling an image:** use `lv_image_set_scale` (with the pivot where it's placed). `LV_IMAGE_ALIGN_STRETCH` made
  pictures not draw at all in LVGL 9.2.2. Leave scale at `LV_SCALE_NONE` when it's 1:1 so 240 stays identical.
- **`\u` escapes:** some editing tools turn `\uXXXX` in C#/XAML into raw characters. In XAML use `&#xE8BB;`; in C#
  use `char.ConvertFromUtf32(...)` when it matters.
- **Windows reserved names:** `aux`, `con`, `nul` and so on can't be file names (git can't add `aux.png`).
- **Keep slow work off the UI thread** in Knob Hub, as in AudioDesk.

## Apps repository

Apps are released in **Shyam-Sangeeth/KnobHub-Apps** (its own repo):

- Each release is one version of one app, tagged `<id>-v<version>`, with `<id>.knobapp` and `<id>.app.json`.
- `pack-release.ps1` creates releases for versions not yet released, so bump `version` in an app's `app.json` to
  publish an update.
- Update that repo's README (with screenshots in its `docs/`) when an app changes.

## Working on the user's PC

- Knob Hub usually runs from `hub\KnobHub\bin\Debug\...` in the tray. It's single instance: setting the event
  `KnobHub.ShowWindow` opens its window.
- Don't steal focus without need (the user may be gaming), and put windows back as they were.
- Commit and push only when asked. Write commit messages to a file and use `git commit -F` (Windows PowerShell adds a
  BOM otherwise).
- AudioDesk tests: back up and restore `%APPDATA%\AudioDesk\mixer.json`, and don't toggle real Sonar or device
  settings.
