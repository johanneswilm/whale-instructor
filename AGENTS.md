# AGENTS.md — guidance for AI coding agents working on Whale Instructor

Whale Instructor is an open-source management suite for WhalesBot MC101s
robot controllers (sold as E7 Pro / AI S1). It is Linux-first, stdlib-only
Python (no third-party Python deps on Linux; the one deliberate exception
is the optional `hid` extra that the USB uploader pulls in on
Windows/macOS — see whale_cli.py), with a vanilla-JS web IDE. Keep it
that way unless asked otherwise.

## Ground rules (do not violate)
1. **Trademark.** The project is called *Whale Instructor*. It is not
   affiliated with WhalesBot. New executables, packages, modules and
   user-facing identifiers must NOT contain "whalesbot". The Python
   dialect import is `from whale import ...`; the C wrapper header is
   `whale_instructor.h`. Historical vendor files that survive in
   `attic/` keep their vendor names — do not rename them, they are ABI
   reference material, not ours.
2. **Vendor material stays private.** `attic/` is gitignored research
   material (see its own AGENTS.md for specifics) and, like `reference/`
   and `vendor/`, must never be committed, published, or copied into
   `publish/`. `make_publish.py` copies a strict whitelist — extend it
   deliberately, never broadly.
3. **Licensing.** Project code: GPL-3.0-or-later with SPDX headers
   (`SPDX-License-Identifier: GPL-3.0-or-later`). Files linked INTO user
   programs (`runtime/whale_instructor.h`, `runtime/assert_override.c`,
   `runtime/user_api/`): LGPL-3.0-or-later. Generated C must carry NO
   SPDX/GPL markers — it belongs to the program's author
   (`LICENSE_EXCEPTION.md`). New source files get SPDX headers;
   `make_publish.py` enforces this.
4. **No comments** in code unless the file already uses them liberally;
   match surrounding style. Python is stdlib-only; JS is framework-free
   (Blockly + CodeMirror are vendored in `whale_instructor/static/vendor/`).

## Architecture map

The installable package is `whale_instructor/` (PyPI name
`whale-instructor`). The old root-level script names (`py2c.py`,
`build_tc.py`, ...) still work as thin shims that import the package and
forward `sys.argv`, so every documented command is unchanged. Path
resolution lives in `whale_instructor/paths.py`: `PACKAGE_DIR`,
`REPO_ROOT` (dev-workbench detection via `make_publish.py`, else None)
and `data_dir()` (per-OS user data dir, created on demand).

- `whale_instructor/py2c.py` — Python-to-C transpiler. `API_FUNCS`/`API_CONSTS` are the
  device API tables (signature kinds: 'i' int, 'f' float); `API_HELP` is
  the user-facing reference served by the IDE's Help tab and
  `/api/help`. If you add a device function, update ALL THREE plus
  `runtime/user_api/` (wrapper + public header). `display_custom` is a
  special form (8 row bitmasks -> LedMaritx struct, an alias of the open
  core's wb_led_t). Entry points: `main` ->
  `user_main`; `task`/`task1`..`task15` -> `user_taskN`. `--target js`
  emits async JS for the `whale` BLE runtime instead of C (`user_taskN`
  bodies wrapped in `while (true) { ...; if (whale.stopped) break; }`,
  every device call `await`ed, `//` -> `Math.floor`, `%` -> `whale.mod`,
  constants -> `whale.NAME`); line-numbered errors are shared with the C
  backend. A device-object sugar layer is desugared before emission
  (`SUGAR_DEVICES` Motor/TouchSensor/InfraredSensor/ColorSensor/
  UltrasonicSensor/SoundSensor — methods are thin aliases of the flat
  whale functions with the type prefix dropped, no enum classes; flat
  constants only, attribute access is an error, `BUILTINS` abs/min/max,
  `wait` alias, docstrings stripped, kwargs on API funcs/methods) — both
  backends accept it. `--dump-api PATH` exports the API + sugar to JSON;
  `make_publish.py` regenerates `static/api.json` from it.
- `whale_instructor/build_tc.py` — compiles/links an image with any arm-none-eabi-gcc.
  Accepts .c or .py input (transpiles via py2c first). Slot-specific linker
  scripts in `runtime/user_api/linker/` (P1/P2/P3 MUST match the upload
  slot). Detects `user_taskN` and adds `-DUSER_TASKN`. Links
  `runtime/open/libwhale_open.a` + `runtime/open_core/libwhale_core.a`
  (grouped) — the build is fully vendor-free; if either
  archive is missing it says to run `python3 -m
  whale_instructor.tools_openlibs`. Compiles the vendored stock ST
  `system_stm32f10x.c` per slot with `-DVECT_TAB_OFFSET`
  (0x19000/0x39000/0x59000). Toolchain discovery order:
  `WHALE_INSTRUCTOR_TOOLCHAIN` env, `~/.local/<tc>/bin` (xPacks, newest
  first), `data_dir()/toolchains/*/bin` (`whale-fetch-toolchain fetch`,
  newest first), then PATH.
- `whale_instructor/whale_cli.py` — USB HID uploader/CLI (VID:PID 2018:5750). Live ops:
  motor (A..D, -100..100), sensor read (ir/touch only — the controller
  reports nothing else live over USB), probe, upload.
- `whale_instructor/serve_ide.py` — localhost HTTP server: no server-side
  program store (a program is a file the user opens/saves wherever they
  like; build/run/transpile requests carry the code inline),
  `/api/ping` liveness canary, `/api/save_local` + `/api/read_local`
  (absolute paths only; the desktop shell's native dialogs and
  launch-arg open), toolchain discovery, build/upload jobs with
  polled logs, `/api/help`, `/api/debug/*` (uses whale_cli.Controller
  directly under DEVICE_LOCK; refuses while a build/upload job runs),
  `/api/transpile {name,code[,target=c|js]}` for the C tab and Run live,
  `/api/quit` (header Quit button; shuts serve_forever down cleanly), and
  `whale-ide --install-launcher/--uninstall-launcher` desktop entries
  (Linux .desktop / Windows Start Menu .cmd / macOS .app bundle, all
  wrapping the resolved `whale-ide` path). Frozen (PyInstaller) builds
  re-dispatch build/upload jobs through the bundle executable via
  `WHALE_BUNDLE_MODULE` (see module_cmd and root tools_build_bundle.py).
  Static assets ship inside the package (`whale_instructor/static/`).
- `whale_instructor/static/js/whale_ble.js` — the `whale` runtime for "Run
  live over Bluetooth": WebBluetooth NUS link (20-byte 'wh' frames,
  request-id matched responses, ~300 ms timeout), sensor cache with a
  ~20 Hz background poll, `stopped` flag/`stop()`, and the device API the
  JS backend emits (APIs with no BLE equivalent throw "not supported over
  Bluetooth"). Pure frame helpers are node-testable via module.exports.
  Grayscale line thresholds mirror the vendor C (whalesbot.c): readings are
  0..100 with dark = HIGH, so black line = above ~60 (5-in-1) / ~50
  (single), white line = below ~20.
- `desktop/` — Tauri v2 desktop shell (Rust + vanilla JS, not part of the
  published package). `src-tauri/src/main.rs` spawns the regular backend on a
  free port in 18765..18784, polls until it answers, then shows a window on
  the backend-served frontend; on exit it POSTs `/api/quit` and kills the
  child. Backend search order differs by build: release prefers the bundled
  sidecar (self-contained app), debug puts `python3 -m
  whale_instructor.serve_ide` second — a PyInstaller sidecar carries stale
  baked-in static files, the module always serves the current checkout
  (PYTHONPATH is pointed at it automatically). WebBluetooth in the webview
  comes from `src-tauri/bridge/ble-shim.js` (injected by Rust), a
  WebBluetooth-compatible wrapper over tauri-plugin-blec so `whale_ble.js`
  runs unchanged; `bleAvailable()` in app.js treats `window.__TAURI__` as
  BLE-capable. The shim is defensive about BlueZ state: it stops any stale
  scan and drops stale plugin-held links before connecting, and retries
  "In Progress" / "Characteristic not available" with backoff — without
  that, one failed attempt wedges every later one. tauri-plugin-dialog
  provides the native save dialog (and `dialog:allow-save` must be in
  the `backend-remote` capability — Tauri permissions are origin-scoped,
  so the backend-served origin needs its own grant even when the splash
  page's capability has it). The shell also accepts a program file as a
  launch argument (`whale-instructor-desktop file.py`) and opens it via
  `?file=<abs path>` + `/api/read_local`; plain Save then writes back to
  that file. Release builds need the PyInstaller
  bundle copied over
  `src-tauri/binaries/whale-instructor-<triple>` (see desktop/README.md).
- `whale_instructor/static/js/whalepy.js` — dependency-free in-browser
  transpiler of the same Python dialect with the same sugar and the same
  line-numbered errors; driven by `static/api.json` via `setApi()`.
  `TestWhalepyParity` in `tests/test_py2c.py` asserts byte-identical JS
  output with the backend for every sample program — keep it green.
- `whale_instructor/static/app.js` — Blockly block definitions + Python generator +
  tabs (Blocks/Python/C/Debug/Help) + dirty-tracking warnings
  (`pyDirty`, `cDirty`) + the "Run live" toolbar button (WebBluetooth
  feature-detect; transpiles via `/api/transpile {target:'js'}`, runs the
  result as async tasks against `window.whale`, button becomes Stop).
  CRITICAL: `pyGen.scrub_` is overridden to walk next-connection chains —
  Blockly 9's base Generator.scrub_ does NOT chain, so without the
  override only the first block of every stack would be emitted.
  "Open file…"/"Save as…" use native browser file dialogs (File System
  Access API with download fallback) and work in both backend and static
  mode; in the Tauri shell "Save as…" goes through the native dialog via
  tauri-plugin-dialog and the backend writes the file (POST
  /api/save_local, absolute paths only), and a plain "Save" button reuses
  the last location (persisted in localStorage). Falls back to **static
  mode** when no backend answers: client-side
  transpile via whalepy.js, programs in localStorage, C/Debug tabs and
  Build/Upload hidden, Export button shown.
  Settings (`whale_hw` in localStorage; dialog via
  the gear button) filter the toolbox by what the user owns (default
  preset = WhalesBot E7 Pro / AI S1 kit) and hold `advanced` (simple mode
  hides the toolchain selector and plain Build button — both only appear
  for a USB connection anyway; Build+Upload is the one-button flow and
  uses the preferred toolchain) plus the preferred `toolchain` (selector
  only shown when >1 compiler is found). Port dropdowns on blocks are never
  filtered (kids re-plug constantly). i18n in `whale_instructor/static/i18n.js`
  (19 languages incl. RTL Arabic/Hebrew: switching to/from them
  re-injects the Blockly workspace with `rtl`, `t()`/`tf()`, block text
  via `%{BKY_WHB_*}` -> `Blockly.Msg`).
- `whale_instructor/runtime/` — `whale_instructor.h` (includes the
  public `user_api/whale_api.h`), `assert_override.c` (LGPL) and
  `runtime/user_api/` — the user layer (LGPL):
  `whale_api.h` (full public API: types/enums/wrappers/patrol engine),
  `app_main.c` (FreeRTOS app framework), `api_wrappers.c` (C API
  wrappers + JY_DO + omni wheel), `patrol.c` (line-patrol engine),
  `syscalls.c` (newlib stubs), `linker/stm32_flash{1,2,3}.ld`.
  `runtime/open/` — vendored FreeRTOS V9.0.0 + ST StdPeriph + CMSIS +
  stock ST startup/device-support (free sources, see its README) plus
  our FreeRTOSConfig.h and stm32f10x_conf.h. Rebuilt by
  `tools_openlibs.py` into `libwhale_open.a`.
  `runtime/open_core/` — the Whale Instructor open core
  (`libwhale_core.a`, LGPL): board/display/control/audio layers
  (hardware facts in its README). `build_tc.py` links
  `libwhale_open.a + libwhale_core.a` as a group — NOTHING
  vendor-derived is compiled or linked.
- `whale_instructor/tools_fetch_toolchain.py` — fetches the arm-none-eabi
  cross compiler (xPack prebuilt, GPL; resolved live from the xPack
  GitHub releases API with a pinned fallback) into
  `data_dir()/toolchains/`, which build_tc auto-discovers. This is the
  ONLY external dependency; the build needs no downloads of any other
  kind and none may be re-added.
- `tests/test_py2c.py` — transpiler suite (103 checks: C + JS backends, sugar
  layer, whalepy parity via node; also compiles samples with
  `-fsyntax-only` when a cross-gcc exists).
  `tests/ide_generators.test.js` — headless Blockly->Python checks plus
  BLE frame-builder checks plus whalepy checks (275 checks; runs Blockly
  in a node VM, no browser).

## Common tasks

```
python3 py2c.py whale_instructor/progs/py_demo.py -o /tmp/user_main.c   # transpile (root shim)
python3 py2c.py whale_instructor/progs/py_demo.py --target js -o /tmp/live.js  # transpile for Run live
python3 -m whale_instructor.py2c --dump-api /tmp/api.json                   # export API tables to JSON
python3 build_tc.py /tmp/user_main.c 1 /tmp/APP_1.bin                   # build (slot 1)
python3 build_tc.py whale_instructor/progs/py_demo.py 1 /tmp/APP_1.bin  # build straight from .py
python3 whale_cli.py probe                                              # detect device
python3 tests/test_py2c.py                                              # py2c tests
node tests/ide_generators.test.js                                       # IDE tests
python3 ide/serve_ide.py --no-open                                      # run IDE (root shim)
python3 make_publish.py                                                 # stage publish/
pipx install whale-instructor                                           # PyPI install
whale-fetch-toolchain fetch                                             # fetch arm-none-eabi-gcc
```

Run BOTH test suites after touching py2c, the IDE, or the API tables.
There is no linter configured; keep changes consistent with the file.

## Device/hardware facts that bite (learned the hard way)

- Stop the vendor app / Wine bridge before any upload: its periodic USB
  traffic corrupts in-flight images.
- Build with the linker script of the TARGET slot; a slot-1 image in P2/P3
  reads .data from whatever sits in slot-1 flash and crashes.
- `get_integrated_grayscale` without a prior
  `patrol_integrated_initialization()` hard-faults the controller.
- Encoder timers run in TI1 mode (both edges of TI1, direction from
  TI2 = 2x per quadrature line) — NOT TI12: the speed loop's goal units
  are calibrated for the 2x count, and TI12 doubles the plant gain,
  which makes the closed loop oscillate (slow "tractor" sound, average
  speed below the goal). Found by A/B-ing encoder counts against the
  vendor firmware (2026-09-09).
- Builds use `-ffunction-sections -fdata-sections` so `--gc-sections`
  can drop unused wrapper/engine functions, and `-u _printf_float`
  stays (newlib-nano's float printf would otherwise be silently
  unavailable to user C programs printing through the debug usart).
- newlib asserts crash via semihosting on modern toolchains —
  `runtime/assert_override.c` (LGPL) stubs `__assert_func`.
- The 0b upload token varies per session and appears opaque; uploads are
  verified with `d8 43`.

## Known open items (see TODO.md)

- Open hardware validation: the patrol engine on a real line track
  (the rover demo validated the raw-threshold path), PO16 servos
  (no servos in the E7 Pro kit), omni wheels (no kit), and the
  never-implemented API functions (get_temperature/get_humidity/
  get_bt_remote_control/display_screen/clear_screen/reverse_motor).
- The toolchain is device-validated end to end: motors (A..D, closed
  loop, A/B'd against the vendor firmware — identical after the
  encoder-mode fix), display (digits, patterns, emotions, symbols),
  audio (A/B'd against the vendor library), the JY_AI analog sensor
  family (touch/IR/ambient/single-gray), grayscale + line following +
  IR seek + bumper back-off (progs/rover_demo.py A/B). The per-layer
  record with all hardware-found fixes lives in
  `runtime/open_core/README.md`.
- 5-in-1 grayscale over the USB LIVE interface carries only ONE real
  reflectance input (port 3, HIGH = bright; ports 1/2/4 are constant 0,
  port 5 an unconnected floating analog pin ~3700 — the vendor app's "92").
  The five real channels only exist via the on-device smart-sensor read:
  uploaded programs (`get_integrated_grayscale`) or Bluetooth — and
  on-device the 5-in-1 must be plugged into P5: that port carries the
  smart-sensor UART5 data line (P3 gets nothing even though the
  sensor's LEDs light there — every port provides power, only P5
  provides data).
- The Bluetooth live-run path (`static/js/whale_ble.js`, vendor web
  IDE's recovered 20-byte frame protocol) is user-validated on
  hardware (motors via the Tauri shell); the open core
  implements the same protocol in wb_control.c's USART2 handler.
- Help-tab descriptions and log/server messages are English-only; block
  text and UI strings are translated.
