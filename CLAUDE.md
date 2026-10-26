# Knob Hub

A PC controller on the Elecrow CrowPanel 1.28" ESP32-S3 rotary display (round 240 x 240 touch screen in a turnable,
pressable knob). Two halves:

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

## Commands

```bash
# knob-os (build output goes to %USERPROFILE%\.cache\knob-os, outside OneDrive)
pio run -e sim -t exec      # simulator window (SDL2); Knob Hub connects to it on 127.0.0.1:5718
pio run -e preview          # knob_preview.dll for Knob Hub's live preview
pio run -e crowpanel        # the real board (hardware layer src/hal/esp32 still mostly stubs)

# hub
dotnet build KnobHub\KnobHub.csproj          # also builds/copies knob_preview.dll if missing
KnobHub\bin\Debug\net10.0-windows10.0.19041.0\KnobHub.exe
dotnet build Plugins\Counter -c Release       # -> bin\Release\Counter.knobapp
powershell -ExecutionPolicy Bypass -File Plugins\pack-release.ps1 [-NoPublish]   # publish apps to KnobHub-Apps
```

Editing `platformio.ini` wipes PlatformIO's build folders, so rebuild both `sim` and `preview` afterwards.

There are no unit tests. Check changes in the **simulator** and in Knob Hub's **live preview** (a virtual knob when no
simulator is connected), and keep `design/screenshots/` current when screens change.

## Layout

**knob-os/src/core**
- `knob_os.cpp`: startup.
- `home.cpp`: the Home ring.
- `apps.cpp` and `icons.cpp`: the cached app list (`apps.json`) and icon cache (`icons.bin`, format "KIC1").
- `link.cpp/.h`: the protocol. Every message is documented in `link.h`, in the `hub_link` namespace (not `link`,
  which clashes with newlib's `link()`).
- `settings.cpp`, `message.cpp`, `toast.cpp`.
- `ui.cpp/.h`: shared widgets, plus the ink helpers `ui::ink_top` and `ui::ink_height` used to centre text visually.
- `theme.h`: colours.

**knob-os/src/hal**
- `sim`: SDL, mouse only, fixed-size window.
- `preview`: the DLL, with in-memory storage.
- `esp32`: LittleFS.

**hub/KnobHub**
- `Core/KnobSession.cs`: talks to one knob (apps, screens, icons).
- `Core/KnobConnection.cs` (simulator TCP) and `Core/KnobPreview.cs` (the DLL).
- `Core/KnobIcons.cs`: draws A8 icons.
- `Core/AppCatalog.cs`: the GitHub app list.
- `Core/HubSettings.cs`: `%LOCALAPPDATA%\KnobHub\settings.json`.
- `Apps/AppManager.cs` (install, order, settings, `.knobapp`), `Apps/AppHost.cs`, and `Apps/MediaApp.cs` (built in).
- `MainWindow`: the Knob, Apps and Settings pages, with its own title bar via `WindowChrome`.
- `AddAppsWindow`: the modal Add apps window.

**hub/KnobHub.Sdk**
- `KnobApp`, `IKnobHost`, `AppSetting`, `KnobIcon`, and the screen types.

**hub/Plugins**
- `AudioDesk` talks to AudioDesk's local API (named pipe `AudioDesk.Api`, AudioDesk repo `LocalApi.cs`). Keep both
  sides in step.
- `Counter` is the small example app.

## Things that bite

- **Icons are cached on the knob by hash.** If you change how `KnobIcons` draws, bump `DrawVersion` in
  `KnobIcons.cs` so knobs fetch icons again.
- **Fonts:** `knob-os/fonts/make_fonts.py` generates them (Montserrat Medium + Latin ranges + LVGL symbols, plus
  digits-only big fonts). A character outside them draws as a box. Knob Hub sends text in other scripts as pictures
  (`TextPicture`).
- **Line breaks:** `LV_TXT_BREAK_CHARS` deliberately has no hyphen, so "Wi-Fi" never breaks.
- **Centring:** centre visually, using ink metrics, not the line box. The selection outline is 2 px, drawn tight to
  the control (no padding).
- **The ESP32 build needs `-D LV_KCONFIG_IGNORE`** (already set).
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
