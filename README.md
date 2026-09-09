<div align="center">

<img src="whale_instructor/static/favicon.svg" alt="Whale Instructor logo" width="120">

# Whale Instructor

**An open-source management suite for WhalesBot hardware.**

</div>

Whale Instructor is an independent open-source project and is not
affiliated with, sponsored by, or endorsed by WhalesBot.

It is a Linux-first programming suite for the WhalesBot MC101s controller
(sold as WhalesBot E7 Pro / AI S1): a visual Blocks editor that grows with
you into real Python and real C, a Python-to-C transpiler, a build
pipeline for any modern arm-none-eabi toolchain, a USB uploader, a live
debug panel, and a searchable reference of the device API.

All project-authored code is GPL-3.0-or-later; the small runtime your
program is built against is LGPL-3.0-or-later. **Programs you write with
Whale Instructor stay yours** — see `LICENSE_EXCEPTION.md`.

## How it fits together

```
Blocks  ──►  Python (whale dialect)  ──►  py2c.py  ──►  user_main.c ─┐
                                                                     ├─► build_tc.py ──► APP_N.bin ──► whale_cli.py upload
C (advanced, editable) ──────────────────────────────────────────────┘
```

- **Blocks** (beginners): drag-and-drop blocks for motors, sensors, sounds,
  displays, line following and more — the full block set of the vendor's
  desktop app. They generate exactly the Python dialect below.
- **Settings**: the default hardware profile is the WhalesBot E7 Pro /
  AI S1 kit (3 motors, 2 touch sensors, 1 infrared, 1 five-in-1 grayscale,
  2 emotion LED screens), so the toolbox only shows what kids actually
  have. Additional kit/add-on hardware can be enabled there at any time.
  Port dropdowns on the blocks always offer every port — kids simply pick
  the port they plugged the part into (it is like Lego: re-plug freely,
  no settings needed). By default the IDE runs in simple mode: one
  **Build + Upload** button and no compiler choice. *Show advanced
  controls* in the settings enables the compiler selector (only shown
  when more than one `arm-none-eabi-gcc` is installed) and a plain Build
  button; the preferred compiler is remembered.
- **Python**: plain, readable Python (`from whale import ...`). Build
  compiles it through the transpiler, with line-numbered errors.
- **C**: an editable C editor. *Take C from Python* translates your current
  Blocks/Python program into C right in the tab (transpiled fresh, never a
  stale artifact), you can edit it and build it directly; building from
  Blocks/Python warns before overwriting your hand-edited C.
- **Debug**: drive any motor at -100..100 and read the sensors live
  (over USB: infrared, touch, ambient light, sound, flame, magnetic,
  ultrasonic, color, single grayscale, and a single reflectance channel
  of the 5-in-1; over Bluetooth all five 5-in-1 channels), without
  writing a program. Sensor rows stream readings until you stop them.
- **Help**: the complete device API with signatures and descriptions —
  click a function to insert its call.
- **Run live** (Chrome/Edge or the desktop app): run your Blocks or
  Python program **live over Bluetooth** — see below.

The IDE speaks 19 languages: English, Deutsch, Español, norsk bokmål,
svenska, dansk, suomi, français, italiano, português, Nederlands, polski,
русский, 中文（简体）, 日本語, 한국어, Türkçe, العربية and עברית (switch in
the title bar; Arabic and Hebrew mirror the whole UI right-to-left,
including the block workspace).

## Linux first (macOS / Windows planned)

This project is developed on Linux and works on Linux today. macOS and
Windows support is planned but not implemented yet (the HID uploader
currently uses `/dev/hidraw`; the toolchain finder searches PATH and can be
pointed at any `arm-none-eabi-gcc` via `WHALE_INSTRUCTOR_TOOLCHAIN` or
`--gcc=`). Everything is stdlib-only Python with argv-list subprocesses, so
nothing is Linux-specific by design.

## What you need

Everything needed to build controller images ships inside the package:
the complete runtime is free software — the Whale Instructor open core
(`runtime/open_core/`, clean-room board/display/control/audio layers),
FreeRTOS V9.0.0, ST's StdPeriph drivers, CMSIS and the stock ST
startup/device support (`runtime/open/`), and the user-API layer your
program compiles against (`runtime/user_api/`). No downloads, no vendor
material, no extraction step. Build the two runtime archives once:

```
python3 -m whale_instructor.tools_openlibs
```

### The cross-compiler (free software, fetched automatically)

Building images needs `arm-none-eabi-gcc`. It's GPL-licensed free
software, so the fetch tool can install it for you — it resolves the
latest xPack prebuilt release for your platform, unpacks it into
`~/.local/share/whale-instructor/toolchains/` (the per-user data dir) and
builds auto-discover it there:

```
whale-fetch-toolchain fetch
```

Or use your system package manager instead — the build finds it on PATH:

```
sudo apt install gcc-arm-none-eabi      # Debian/Ubuntu
sudo dnf install arm-none-eabi-gcc      # Fedora
brew install arm-none-eabi-gcc          # macOS
```

## Install (PyPI)

```
pipx install whale-instructor       # provides whale, whale-ide, whale-fetch-toolchain
python3 -m whale_instructor.tools_openlibs   # build the runtime archives (once)
whale-fetch-toolchain fetch         # free arm-none-eabi-gcc (auto-discovered)
whale-ide                           # open the IDE at http://127.0.0.1:8766/
```

Instead of `fetch toolchain` you can install `gcc-arm-none-eabi` via your
system package manager; the build auto-discovers both (and `--gcc=` /
`WHALE_INSTRUCTOR_TOOLCHAIN` override it).

## Quick start (command line, from a source checkout)

```
# 1. transpile Python (the whale dialect) to C
python3 py2c.py whale_instructor/progs/py_demo.py -o /tmp/user_main.c

# 2. build a controller image (auto-finds a toolchain; newest ~/.local
#    xPack first, then fetched toolchains, then PATH — or pass
#    --gcc=/path/to/arm-none-eabi-gcc)
python3 build_tc.py /tmp/user_main.c 1 /tmp/APP_1.bin

# 3. upload to program slot P1 (stop the vendor app first — it fights
#    over the USB device)
python3 whale_cli.py probe                 # must answer MC1102
python3 whale_cli.py upload /tmp/APP_1.bin 1
```

The root-level `py2c.py` / `build_tc.py` / `whale_cli.py` /
`tools_*.py` / `ide/serve_ide.py` are thin shims over the
`whale_instructor/` package, so these commands work unchanged; the same
tools are also available as `python3 -m whale_instructor.<module>` and,
after a PyPI install, as the `whale*` commands above.

## Quick start (IDE)

```
whale-ide                           # or: python3 ide/serve_ide.py
                                    # http://127.0.0.1:8766/
```

One pipeline: Blocks/Python → py2c → build_tc.py → upload, with build and
upload logs streamed live (the log stays hidden until you open it from the
bar at the bottom).

## Files: open / save

Programs are ordinary files and live wherever you keep them — the IDE
does not keep a program store of its own. **Open file…** loads any
`.py`/`.txt` (or `.c`) file from disk into the matching tab (with the
usual unsaved-changes warning), **Save as…** writes the current program
wherever you pick, and plain **Save** reuses that location. In the
browser this uses the File System Access API (Chrome, Edge; other
browsers fall back to a download); the desktop app uses native dialogs
and writes through the local backend. In static mode (page served
without the backend) programs are kept in the browser's localStorage.

## Desktop launcher and Quit

`whale-ide` normally runs in a terminal. To give it a double-clickable
icon instead:

```
whale-ide --install-launcher     # Linux: ~/.local/share/applications/
                                 # Windows: Start Menu shortcut (.cmd)
                                 # macOS: ~/Applications/Whale Instructor.app
```

The launcher runs the IDE without a terminal window; the **Quit** button
in the IDE's header (backend mode) then shuts the server down cleanly.
`whale-ide --uninstall-launcher` removes the entry again. Re-running
install/uninstall when already installed/uninstalled is a no-op.

## Standalone app bundle

No Python on the machine? Build a self-contained desktop app with
[PyInstaller](https://pyinstaller.org/) (a build-time tool only — the app
itself stays stdlib-only):

```
python3 -m venv /tmp/bundle-venv
/tmp/bundle-venv/bin/pip install pyinstaller
/tmp/bundle-venv/bin/python tools_build_bundle.py            # one file
/tmp/bundle-venv/bin/python tools_build_bundle.py --onedir   # faster start
```

Output lands in `artifacts/bundle/` (`whale-instructor` binary, or
`Whale Instructor.app` on macOS). The bundle starts the IDE server and
opens the browser like `whale-ide` does; the **Quit** button in the IDE
shuts it down. Build/upload jobs re-dispatch through the bundle
executable, so everything works without a system Python.

## Desktop app (Tauri)

`desktop/` holds a thin Tauri v2 shell that spawns the same IDE backend
and shows it in a native window — with Bluetooth working through a
WebBluetooth-compatible bridge, so **Run live** works there too. The
shell also accepts a program file as a launch argument and opens it
straight away:

```
cd desktop && npm install && npm run tauri dev     # development
whale-instructor-desktop rover_demo.py             # installed app
```

A program opened this way keeps its file identity: plain **Save**
writes back to the opened file, just like after a "Save as…".

## Run live over Bluetooth

The toolbar's **Run live** button (shown only when the browser has
WebBluetooth, i.e. Chrome or Edge) runs the current Blocks or Python
program **live on the controller over Bluetooth**, instead of compiling and
uploading it. The Python dialect is transpiled to async JavaScript
(`py2c.py --target js`) and executed against the `whale` BLE runtime
(`whale_instructor/static/js/whale_ble.js`), which speaks the same 20-byte
Nordic-UART frame protocol as the vendor's web IDE. The button turns into
**Stop** while a program runs; disconnecting the link stops it too.

The transpile step runs **in the browser**: `whale_instructor/static/js/whalepy.js`
is a dependency-free implementation of the same dialect (same errors, same
output) driven by the machine-readable API dump `whale_instructor/static/api.json`
(regenerate with `python3 -m whale_instructor.py2c --dump-api PATH`). This is
what lets the IDE fall back to **static mode** when no local backend answers
(e.g. the page is served from anywhere as plain files): Blocks → Python →
Run live keep working; Build/Upload, the C tab and the program-file server
need the app (`whale-ide`). An *Export* button downloads the current program
instead.

What works live: motors (incl. timed and angle moves, dual motor, the
`move()`/`move_time()` drive helpers), servos, the LED matrix (symbols,
emotions, custom 8x8 patterns), RGB lights, the electromagnet, sounds, and
essentially all kit sensors (touch, infrared, 5-in-1 and single grayscale,
ultrasonic, ambient light, temperature/humidity, flame, magnetic, sound
volume, encoders, color, controller keys). Sensor values are cached and
refreshed in the background (~20 Hz), so tight loops don't flood the link.

What does **not** work over Bluetooth (the runtime raises a clear
"not supported over Bluetooth" error, shown in the log pane): the C tab,
program upload (USB only — the vendor's own web IDE cannot flash over BLE
either), EEPROM, patrol/line following, omni wheels, the digital tube /
controller screen / recorder, AI camera/voice modules and the raw
digital/analog IO pins. `off_LED`/`off_emotion` and line/obstacle
*detection* helpers are emulated client-side (pattern id 0 clears a matrix;
detection uses the vendor-matching thresholds `whale.grayBlackThreshold` /
`whale.grayWhiteThreshold` for the 5-in-1 and `graySingle*` for the single
sensor, plus the tunable `whale.irObstacleThreshold`),
and `move()` assumes the usual wheel pairing (left = A, right = B —
override via `whale.wheels`).

Live run needs WebBluetooth (Chrome, Edge, or the bundled desktop app;
not Firefox) and a machine that already talks BLE. This path is built
from the recovered vendor protocol and is partially device-tested
(connect + motor runs verified); the USB build+upload pipeline remains
the fully validated one — see `TODO.md` for the remaining checklist.

## The Python dialect

```python
from whale import A, set_motor, sleep

def main():
    set_motor(A, 50)
    sleep(3000)
    set_motor(A, 0)
```

- entry points: `def main()` (runs once at boot) and `def task()`,
  `def task1()` … `task15()` (loop tasks; the build adds `-DUSER_TASKN`)
- explicit imports only (no star imports), numeric values only
  (no strings/lists/`print` — use `display_digital_tube(P1, n)`)
- `if`/`elif`/`else`, `while`, `for i in range(a, b[, literal step])`,
  `break`/`continue`/`return`/`pass`, `//`, `%`, float `/`,
  helper functions, `and`/`or`/`not`, `+=`/`-=`/`//=` etc.
- ~80 device API functions and constants — motors, omni wheels, servos,
  sounds, LED matrices, grayscale, patrol/line following, sensors, timers,
  EEPROM. The whole list with descriptions lives in the IDE's Help tab;
  the machine-readable source of truth is `py2c.py` (`API_HELP`), exported
  to `whale_instructor/static/api.json` for the browser.
- `display_custom(port, r0..r7)` draws an 8x8 LED matrix pattern from
  eight row bitmasks.

### Device objects (ergonomics over the flat API)

The dialect is **not** Pybricks-compatible — it offers the same *kind* of
ergonomics over whale's own API. On top of the flat functions there is a
thin object layer, accepted by both backends (C builds and Run live) and
stripped before emission. Everything it spells exists in the flat API; the
method names are the flat function with its type prefix dropped:

```python
from whale import Motor, InfraredSensor, B, C, wait

arm = Motor(B)
eye = InfraredSensor(C)

def main():
    arm.set(50)                       # -> set_motor(B, 50)
    arm.set_time(50, 2.5)             # -> set_motor_time(B, 50, 2.5)  (seconds)
    arm.set_angle(50, 90)             # -> set_motor_angle(B, 50, 90)
    while eye.value() > 5:            # -> get_infrared_distance(C)
        wait(20)                      # -> sleep(20)
    arm.off()                         # -> off_motor(B)
    closest = min(eye.value(), 100)   # abs/min/max need no import
```

- **Docstrings** (triple-quoted first statement of a module or function)
  are allowed and ignored — handy for classroom templates.
- **Keyword arguments** work on device API functions and device-object
  methods (`set_motor(speed=50, motor=A)`); unknown keywords and kwargs on
  your own functions are still errors.
- Constants stay **flat** (`A`..`D` motors, `P1`..`P5` sensor/LED ports,
  `color_red`, …) — there are no dotted enum namespaces; attribute access
  is a transpile error.
- **Device objects** — `Motor(port)`, `TouchSensor(port)`,
  `InfraredSensor(port)`, `ColorSensor(port)`, `UltrasonicSensor(port)`,
  `SoundSensor(port)`. A constructor returns the port as a typed handle;
  the handle may also be passed to the matching flat API functions
  (`set_motor(arm, 50)` works). Methods are thin aliases of the flat
  calls — same arguments after the port, same units, no scaling. Misuse
  is caught at transpile time: unknown methods, handle arithmetic,
  passing a sensor handle to a motor function, etc.

| Object | Method | Calls |
|---|---|---|
| `Motor` | `set(speed)` | `set_motor(port, speed)` |
| | `set_time(speed, seconds)` | `set_motor_time(port, speed, seconds)` |
| | `set_angle(speed, degrees)` | `set_motor_angle(port, speed, degrees)` |
| | `off()` | `off_motor(port)` |
| | `reverse()` | `reverse_motor(port)` |
| | `speed()` | `get_motor_speed(port)` |
| | `angle()` | `get_encoder_value(port)` |
| | `reset_angle()` | `reset_motor_encoder(port)` |
| `TouchSensor` | `pressed()` | `touch_switch_pressed(port)` |
| `InfraredSensor` | `value()` / `obstacle()` | `get_infrared_distance` / `obstacle_infrared_detected` |
| `ColorSensor` | `value()` / `detected(color)` | `color_value` / `color_detected` |
| `UltrasonicSensor` | `value()` | `get_ultrasonic_distance` |
| `SoundSensor` | `value()` | `get_sound_volume` |

- `abs()`, `min(a, b)`, `max(a, b)` are built in (no import); `wait(ms)`
  is an alias of `sleep(ms)`.

Still **not** supported (clear errors, by design): strings beyond
docstrings, lists/collections, user classes, `DriveBase` (no encoder
odometry), `in`, subscripting, tuple unpacking, `**`, attribute access.

Errors are reported with line numbers, both at transpile time and in the
IDE log.

## Repository layout

| Path | Purpose |
|---|---|
| `whale_instructor/` | the installable Python package (PyPI: `whale-instructor`) |
| `whale_instructor/py2c.py` | Python -> C transpiler (`--target js` for live Bluetooth), device API tables + reference, device-object sugar layer, `--dump-api` |
| `whale_instructor/static/js/whalepy.js` | dependency-free in-browser transpiler of the same dialect (parity enforced by tests) |
| `whale_instructor/static/api.json` | machine-readable API dump driving whalepy.js (regenerated by `make_publish.py`) |
| `whale_instructor/build_tc.py` | build images with any modern arm-none-eabi toolchain (accepts `.py` directly) |
| `whale_instructor/whale_cli.py` | USB HID uploader / probe / live motor & sensor CLI |
| `whale_instructor/serve_ide.py` + `whale_instructor/static/` | local web IDE (stdlib server + Blocks/Python/C UI) |
| `whale_instructor/runtime/` | LGPL runtime your program is built against: `whale_instructor.h`, `assert_override.c`, `user_api/` (public API header, app framework, wrappers, patrol engine, syscalls, slot linker scripts) |
| `whale_instructor/runtime/open/` | free upstream sources: FreeRTOS, ST StdPeriph, CMSIS, stock ST startup/device support + our configs |
| `whale_instructor/runtime/open_core/` | the Whale Instructor open core (`wb_*.c` board/display/control/audio layers) |
| `whale_instructor/tools_openlibs.py` | build `libwhale_open.a` + `libwhale_core.a` from the free sources |
| `whale_instructor/tools_fetch_toolchain.py` | fetch arm-none-eabi-gcc (xPack, GPL) into the user data dir |
| `whale_instructor/progs/` | sample programs (Python dialect and C) |
| `py2c.py`, `build_tc.py`, ... | root shims so the documented commands keep working |
| `desktop/` | Tauri v2 desktop shell (native window, Bluetooth bridge, launch-arg open) |
| `tests/` | py2c suite (`tests/test_py2c.py`) + IDE generator checks |
| `make_publish.py` | builds the publishable `publish/` tree |
| `TODO.md` | open work |

## Tests

```
python3 tests/test_py2c.py          # transpiler + JS backend + whalepy parity: 103 checks
node tests/ide_generators.test.js   # IDE blocks -> Python + BLE frames + whalepy: 275 checks
```

The py2c suite also compiles every sample program with
`arm-none-eabi-gcc -fsyntax-only` against the runtime headers (skipped if
no cross-gcc is found; set `WHALE_INSTRUCTOR_TOOLCHAIN=/path` to point at
one). No controller needed.

## Operational notes

- Stop the vendor application (and any Wine bridge) before uploading with
  `whale_cli.py` — its periodic USB traffic corrupts in-flight uploads.
- Uploads go to slots P1..P3; the finalize packet starts the program, so
  disconnect USB and watch the robot.
- An image must be built for its target slot (build_tc.py takes the slot
  as an argument) — never reuse a slot-1 image in P2/P3.

## License and trademark

- Project code: GPL-3.0-or-later (`LICENSE`); runtime pieces linked into
  your program: LGPL-3.0-or-later; your own programs: any terms you like
  (`LICENSE_EXCEPTION.md`). Third-party notices: `THIRD_PARTY_NOTICES.md`.
- **Whale Instructor is an independent open-source project and is not
  affiliated with, sponsored by, or endorsed by WhalesBot.** The name
  "WhalesBot" and the hardware it describes are trademarks of their
  respective owner; they are mentioned here solely to identify the
  supported hardware.
