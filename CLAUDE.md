# Knob Hub

A PC controller on round ESP32 knob displays. It isn't tied to one board: so far the Elecrow CrowPanel 1.28"
(240 x 240) and 1.46" (360 x 360), later the user's own knob with a motor (SmartKnob style), and others (a friend may
use another size). Screens are always round and boards always ESP32s; everything else (size, touch, LEDs, motors)
varies. Two halves:

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
| cards | `tpl_cards.cpp` |
| media | `tpl_media.cpp` |

Templates are registered in `templates.cpp`. Colours, units, texts and icons all come in the data. The knob reports
input (rotate, press, tap, select, back).

When an app needs something new, add a generic option to a template, or a new template, plus its SDK screen type in
`hub/KnobHub.Sdk/Screens.cs`. Never write app code in the firmware: an app change must never need a firmware update.

## Several boards

Never assume a particular board: read the size from `ui::size()` and features from `hal_board()` (Knob OS) or the
session's `KnobBoard` (Knob Hub), and keep board-specific code in the hardware layer and the pin files.
`knob-os/src/core/boards.cpp` lists the boards (mirrored in `hub/KnobHub/Core/KnobBoard.cs`): screen size, touch,
LED count, vibration motor, motor on the knob. Knob Hub builds an unknown board from its hello, so it doesn't break on
one; the preview falls back to the default board then. A knob reports its board in `hello`; Knob Hub shows only the settings
that board has and makes its preview the same board, but always at 480 px, one pixel per screen pixel (the user's
choice: one preview size, only the settings and LEDs follow the board). `kp_screen(240 or 360)` renders as a real knob
for pixel tests.

- **Screens are designed at 240 px and scaled.** Every length in screen code goes through `ui::px()`, fonts come from
  `ui::font(design size)` / `ui::digits()`, and `ui::size()` is the diameter. Lengths measured at run time (label
  widths, font metrics) are real pixels already: don't scale them. At 240 nothing may change, pixel for pixel.
- **Fonts per size:** `knob-os/fonts/make_fonts.py` generates each diameter's set (`DIAMETERS`: the boards' 240 and
  360, and 480 for the preview) and `fonts.cpp`. A new board size needs adding there.
- **A new board:** an entry in `boards.cpp` and `KnobBoard.cs`, its fonts, a pin file in `src/hal/esp32/boards/`, a
  `platformio.ini` env setting `KNOB_BOARD` and `KNOB_FONT_SET`, and drivers for any chip the ESP32 layer lacks
  (steps in the README's "Adding a board").
- **Haptics** (`haptics.h`): each screen's `feel()` says how turning should feel (detent positions, end stops, wrap,
  strength), derived from what it shows; Knob OS passes it to `hal_motor_feel` and plays `hal_vibrate` effects (tick,
  bump at an end stop, click, alert). Apps never deal with it.
- **Lights** (`lights.h`): each screen's `glow()` gives a colour, an optional level (0-100) and where its progress
  arc is (start, sweep); Knob OS turns it into LED colours by the Lights settings (Level meter, App colour, Off) and
  passes them to `hal_set_leds`. A screen with a new kind of arc returns its own start and sweep. The preview shows
  them as a glow (`ShowLeds`): one LED a halo, several along the arc, one part per LED.
- **Screen timeout** (`idle.h`): `KnobSettings::timeout_seconds` (0 = never; `timeout_seconds` on the link since Knob
  OS 1.0.6). The older `timeout` (a position in 15 s, 30 s, 1 min, 5 min, never) is still sent and taken, for older
  versions and old settings files, on both sides (`settings.cpp`, Knob Hub's `KnobSettings`). The time since the last
  input (LVGL's inactive time; `idle::input()` counts knob turns and presses) dims and then turns off the screen. An
  input while it's off only wakes it: knob inputs are dropped in `knob_os_input`, touches by a layer over everything until the finger lifts. A toast wakes it (`idle::wake`); app
  screen updates don't, or the Media app's position updates would keep it on. An app can keep it on
  (`IKnobHost.KeepAwake`): Knob Hub sends `awake` while any app wants it, again after each hello, and the knob lets
  go when the link drops (`idle::keep_awake`). Settings > Connection and its Wi-Fi screens hold it on the same way
  while they're on show (`settings::keeps_screen_awake`), since a join can take a minute. The knob reports its state
  in an `idle` message, and Knob Hub's preview follows it while mirroring (`follow`), so a wake-up input never acts on
  the preview either.
- **Pictures** (album art, `TextPicture`) carry `screen`, the size Knob Hub drew them for (`KnobSession` adds it);
  Knob OS scales one drawn for another size (`ui::picture_px`), so the 480 px preview can mirror any knob. Touches
  are rescaled between the two in `KnobSession`.

## Commands

```bash
# knob-os (build output goes to %USERPROFILE%\.cache\knob-os, outside OneDrive)
pio run -e sim -t exec      # simulator window (SDL2); Knob Hub connects to it on 127.0.0.1:5718
                            # program.exe --board crowpanel-1.46 (or motor-knob) for another board
pio run -e preview          # knob_preview.dll for Knob Hub's live preview
pio run -e crowpanel-128    # a real board: one env per board; -t upload --upload-port COM7 (the knob's port; PlatformIO
                            # touches it at 1200 baud into the loader, a new port) flashes it. Also makes
pio run -e crowpanel-146    # knob-os.bin and copies it to .cache\knob-os\firmware (scripts/merged.py)

# hub
dotnet build KnobHub\KnobHub.csproj          # also builds/copies knob_preview.dll if missing
KnobHub\bin\Debug\net10.0-windows10.0.19041.0\KnobHub.exe
dotnet build Plugins\Counter -c Release       # -> bin\Release\Counter.knobapp
powershell -ExecutionPolicy Bypass -File Plugins\pack-release.ps1 [-NoPublish]   # publish apps to KnobHub-Apps

# a Knob Hub release (from the repository root): Knob OS for every board, the MSI and the boards' images
powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1    # -> installer\out
```

Editing `platformio.ini` wipes PlatformIO's build folders, so rebuild both `sim` and `preview` afterwards.

There are no unit tests. Check changes with the scripts in `tools/` (see its README), in the **simulator** and in
Knob Hub's **live preview** (a virtual knob when no simulator is connected), and keep `design/screenshots/` current
when screens change.

- **Behaviour:** `python -I tools/checks.py` (LEDs and dial presses, pictures, haptics, toasts, screen timeout,
  cards, the Wi-Fi screen, the busy spinner, keeping the screen awake, push vs tap, the picker, odd app data, scrolling
  text, settings changed while away, the timeout choices) through the preview DLL. Add a check there when you add behaviour.
- **Knob Hub and app code:** `dotnet run --project tools\hub-tests` (the Timer and AudioDesk apps, the timeout setting, app ids, the Add
  apps catalog, the updater's checksum). Add to it when you change that code.
- **New helper scripts** go in `tools/` (with a usage header and a line in its README), not left in a scratch folder.
- **Pixels:** `tools/tour.py` before and after a change, then `tools/compare.py`: screens at 240 must stay identical
  pixel for pixel unless the change is meant to show.
- **Without the user's mouse or focus:** `tools/sim-capture.ps1` posts wheel and middle-button messages to the
  simulator's window (it can stay minimized), and `tools/hub-shots.ps1` captures Knob Hub with `PrintWindow` (it
  backs up and restores the settings). Knob OS's own Settings >
  Display > Brightness is a dial that's safe to open. Don't open apps that act on the PC (AudioDesk, Media) to test,
  and never press on a Displays card: it switches the real main display (turning through them is safe).

## Layout

**knob-os/src/core**
- `knob_os.cpp`: startup.
- `home.cpp`: the Home ring.
- `apps.cpp` and `icons.cpp`: the cached app list (`apps.json`) and icon cache (`icons.bin`, format "KIC1").
- `link.cpp/.h`: the protocol. Every message is documented in `link.h`, in the `hub_link` namespace (not `link`,
  which clashes with newlib's `link()`).
- `settings.cpp`: the knob's settings, kept in `settings.json`. Knob Hub sends its saved settings after every hello,
  and the knob sends its own when one is changed on the knob. Changes made while Knob Hub isn't connected are kept
  (`changed_away`, saved) and go in the next hello, and Knob Hub takes them (not the name) before sending its own.
- `message.cpp`, `toast.cpp`.
- `busy.cpp/.h`: the busy spinner an app's screen can ask for (`busy` in its data), over everything; only Back gets
  through while it shows.
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
- `esp32`: display (LovyanGFX, panel set in the pin file; LVGL renders into two DMA buffers of a third of the screen;
  built with `-O2`, and without the round clip the simulator and the preview have: a whole-screen frame takes ~25 ms,
  against 193 ms before. Measure with a `-D KNOB_PERF` build and `tools/knob_perf.py`), CST816 touch (pushing the
  knob presses the screen too: a touch under way when the button goes down is cancelled, so it isn't also a tap),
  encoder (interrupts, one step per full cycle; turns while it's held in are dropped) and press, backlight PWM, WS2812
  LEDs (RMT), USB link (TinyUSB serial, 303A:0002 from the variant in `knob-os/variants/knob`, started by `start_usb`
  once the knob's name is read, so Windows shows the device and its port under that name; `ARDUINO_USB_CDC_ON_BOOT=0`,
  so the link is `usb_serial`, not `Serial`; only opening it at 1200 baud restarts into the loader, not
  DTR/RTS, which Windows plays when a port closes; lines read by a task on core 0: the USB serial drops what its
  buffer can't hold), LittleFS; one pin file per board in `boards/`, which can
  also set `SCREEN_UPSIDE_DOWN` (screen and touch turned 180° so the top is where the LEDs start; the 1.28" does) and
  `LEDS_REVERSED` (LEDs chained the other way round the ring; the 1.28" does), `LED_AT_TOP` (the LED in the chain at
  the top of the screen; 7 on the 1.46"), `ENCODER_HALF_CYCLE` (a click is half a cycle of A and B; the 1.46") and
  `LCD_RGB_ORDER`, and `WIFI_TX_POWER` (in quarter dBm; the 1.46" needs 60, 15 dBm: at full power its router never
  answered it, reason 2, and with 15 dBm it joins in seconds). Wi-Fi and pairing are in `net.cpp`: a join gets a
  minute (`JoinMs`) and is pinned to the scanned access point only in pairing mode; a failure is reported as
  `join_problem` and the driver's `join_reason`, which Knob Hub logs. While Knob Hub is connected over USB the radio
  is off (`net::usb_link`, state `usb`): it comes on for scans, joins and pairing mode (and `RadioAfterUseMs` after)
  and when the USB link goes. Motors still to do.
  The 1.46"'s ST77961 needs its own start-up commands (`boards/crowpanel_1_46_panel.h`, from the LovyanGFX 1.2.7 that
  Elecrow's example ships): newer LovyanGFX's `Panel_ST77961` leaves it blank. A DFRobot DRI0056 haptic driver (TM6605,
  I2C 0x2D) on the spare I2C connector (`PIN_EXT_SDA/SCL`, `Wire1`) is looked for at start and gives the board
  vibration (`hal_board().vibration`); it answers only a moment after power-on, hence the retries.

**hub/KnobHub**
- `Core/KnobSession.cs`: talks to one knob (apps, screens, icons).
- `Core/KnobConnection.cs`: the links, highest priority first in `KnobLinks` (USB, Wi-Fi, then the simulator's TCP); lower
  ones pause while a higher one is in use. A link counts as connected once the knob says hello (`LineConnection`).
  USB looks only at COM ports with Espressif's vendor id (registry), opened with RTS off (RTS can reset the ESP32-S3).
  The knob doesn't go by DTR (Knob OS before 1.0.2 was on the USB Serial/JTAG unit, which has none), so over USB
  Knob Hub sends a `hub` heartbeat every 2 s with the time (`Heartbeat`); the knob's link is up while they come (6 s without one: down).
- `Core/KnobPreview.cs`: the DLL, always 480 px; it loads a fresh copy from %TEMP% to become another board, since
  Knob OS starts only once per DLL. While it isn't on show (`Shown`: the window in the tray, minimized or on another
  page) it ticks 4 times a second and copies no frames.
- `Core/KnobBoard.cs`: the boards, and the board in a hello.
- `Core/KnobIcons.cs`: draws A8 icons.
- `Core/AppCatalog.cs`: the GitHub app list.
- `Core/HubSettings.cs`: `%LOCALAPPDATA%\KnobHub\settings.json`.
- `Core/KnobFirmware.cs`: the Knob OS images that came with Knob Hub (`firmware\` next to the exe, copied in by the
  csproj's `KnobFirmware` target from `%USERPROFILE%\.cache\knob-os\firmware`).
- `Core/KnobFlasher.cs`: installs an image over USB through the ESP32-S3's ROM loader (Espressif's documented serial
  protocol: SLIP, compressed writes, MD5 check), no buttons. Knob OS's own USB serial (TinyUSB, 303A:0002, see
  `knob-os/platformio.ini`; it keeps the knob out of Safely Remove Hardware) restarts into the loader when opened at
  1200 baud; the loader is on the chip's USB Serial/JTAG unit (303A:1001), a different COM port, where DTR/RTS reset
  the chip into the loader and back. It
  skips the nvs partition (pairing key, Wi-Fi network); the file system with the settings lies past the image.
  `KnobSession.InstallKnobOsAsync` pauses every link meanwhile; `UsbConnection.SilentPorts` are knobs that never
  said hello (not running Knob OS), offered in Settings with a board picker.
- `Core/HubUpdates.cs`: Knob Hub's own updates (latest release of this repo, its `.msi`, SHA-256 checked), as in
  AudioDesk. `UpdateModels.cs` has the Settings page's Knob OS and update models.
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
- `Displays` picks the main display (cards) through Windows' CCD API (`QueryDisplayConfig`, `SetDisplayConfig`).
- `Timer` is a countdown on the ring screen; it keeps the knob's screen on while it runs (`Host.KeepAwake`).

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

## Releases

Knob Hub's releases are in this repo; apps have their own (below).

1. Bump `<Version>` in `hub/KnobHub/KnobHub.csproj`, and `KNOB_OS_VERSION` in `knob-os/src/core/knob_os.h` when Knob
   OS changed (Knob Hub offers knobs the Knob OS it carries when it's newer than theirs).
2. `installer\build-installer.ps1`: `installer\out` gets `KnobHub-<version>-x64.msi` and
   `knob-os-<os version>-<board>.bin` for each board.
3. A GitHub release `v<version>` with all of them attached. The updater reads the latest release's `.msi` and checks
   its SHA-256 digest, and the installer won't downgrade, so never publish a lower version than the latest.

The installer is per-machine (Program Files\Knob Hub by default, a folder page to choose another, kept for upgrades
in `HKLM\Software\KnobHub\Installer\InstallFolder`), starts Knob Hub through Explorer so it doesn't run elevated,
and uses `KnobHub.exe --exit`, `--add-startup` and `--remove-startup`. Commit and publish only when the user asks.

## Apps repository

Apps are released in **Shyam-Sangeeth/KnobHub-Apps** (its own repo):

- Each app has one release, tagged with its id (title: the app's name), holding every version as
  `<id>-<version>.knobapp` and `<id>-<version>.app.json`. Add apps also reads the old layout (a release per version,
  tagged `<id>-v<version>`, with `<id>.knobapp`), so keep that working.
- `pack-release.ps1` adds versions not yet released to their app's release (creating it for a new app), so bump
  `version` in an app's `app.json` to publish an update.
- The **All apps** release (tag `apps`, no files) lists the apps and carries GitHub's "Latest" mark, which can't be
  turned off. `pack-release.ps1` rewrites its notes after publishing and creates app releases with
  `--latest=false`, so they don't take the mark.
- Update that repo's README (with screenshots in its `docs/`) when an app changes.
- The guide to making apps is that repo's **wiki** (`KnobHub-Apps.wiki.git`; pages such as Screens, How-apps-work and
  For-AI-agents, images in `images/`). Update it with the SDK: new screens or fields, input, app loading, packaging,
  built-in icons or Knob Hub's install and Add apps behaviour. Its code samples must compile.

## Working on the user's PC

- Knob Hub usually runs from `hub\KnobHub\bin\Debug\...` in the tray. It's single instance: setting the event
  `KnobHub.ShowWindow` opens its window.
- Don't steal focus without need (the user may be gaming), and put windows back as they were.
- Commit and push only when asked. Write commit messages to a file and use `git commit -F` (Windows PowerShell adds a
  BOM otherwise).
- AudioDesk tests: back up and restore `%APPDATA%\AudioDesk\mixer.json`, and don't toggle real Sonar or device
  settings.
