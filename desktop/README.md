# Whale Instructor — desktop shell (Tauri v2)

A thin desktop wrapper around the regular Whale Instructor stack. The shell
does not reimplement anything: on startup it spawns the normal Python backend
(`whale_instructor.serve_ide`) as a child process on a free port in
18765..18784, waits until `http://127.0.0.1:<port>` answers, and shows a
window that loads the backend-served frontend — same pages, same JSON API,
same behavior as the browser version. On exit it POSTs `/api/quit` and kills
the child if it is still alive.

Bluetooth: WebBluetooth does not exist in the Tauri webview, so
`src-tauri/bridge/ble-shim.js` (injected by Rust as a window initialization
script, desktop builds only) implements just enough of the WebBluetooth API —
`navigator.bluetooth.requestDevice` with `namePrefix` filters, `device.gatt`,
services/characteristics, notifications as
`characteristicvaluechanged` events — on top of
[tauri-plugin-blec](https://github.com/MnlPhlp/tauri-plugin-blec). The frontend
(`whale_instructor/static/js/whale_ble.js`) runs unchanged.

## Layout

```
package.json                      npm scripts + Tauri CLI
src-tauri/
  Cargo.toml                      tauri v2 + tauri-plugin-blec
  tauri.conf.json                 withGlobalTauri, externalBin sidecar
  capabilities/*.json             core + blec permissions, incl. remote
                                  URLs for the backend-served origin
  src/main.rs                     backend spawn/poll/quit, window, shim inject
  bridge/ble-shim.js              WebBluetooth shim over the blec plugin
  binaries/whale-instructor-x86_64-unknown-linux-gnu
                                  sidecar placeholder (see below)
dist/index.html                   splash placeholder (window starts here)
icons/icon.png                    app icon
```

## Development

```sh
npm install
npm run tauri dev        # or: cd src-tauri && cargo run
```

Backend binary search order (see `src/main.rs`). Release builds (the shipped,
self-contained app): `$WHALE_INSTRUCTOR_BIN` → bundled sidecar
(`binaries/whale-instructor-<target-triple>` next to the executable, per
Tauri's `externalBin` convention) → `whale-instructor` / `whale-ide` on
`PATH` → `python3 -m whale_instructor.serve_ide`. Debug builds invert this
after the env override: `python3 -m whale_instructor.serve_ide` comes first
(the child gets `PYTHONPATH` pointed at the nearest checkout automatically,
so it always serves the current `whale_instructor/static/`), then PATH, and
the sidecar last — a previously built PyInstaller bundle carries stale
baked-in static files, which is exactly what you do not want while
developing. For day-to-day development no sidecar setup is needed.

`binaries/whale-instructor-x86_64-unknown-linux-gnu` is a shell placeholder so
`tauri-build` finds the `externalBin` entry. It prefers the real PyInstaller
bundle from `artifacts/bundle/` and falls back to PATH/`python3`, exactly like
the Rust search order.

## Release builds

Build the self-contained backend first (from the repo root):

```sh
python3 tools_build_bundle.py        # -> artifacts/bundle/whale-instructor
```

Then install it as the sidecar (Tauri names sidecars `<name>-<triple>`; the
placeholder must be *replaced*, it ships in the bundle otherwise):

```sh
cp artifacts/bundle/whale-instructor \
   desktop/src-tauri/binaries/whale-instructor-x86_64-unknown-linux-gnu
cd desktop
npm run tauri build                  # -> src-tauri/target/release/bundle/
```

## Linux system dependencies

Tauri needs the GTK 3 / WebKit2GTK 4.1 development packages (names verified
against `apt-cache policy` and pkg-config on Debian/Ubuntu):

```sh
sudo apt install libwebkit2gtk-4.1-dev libgtk-3-dev libsoup-3.0-dev \
                 libjavascriptcoregtk-4.1-dev
```

plus the usual Tauri base prerequisites (`build-essential curl wget file
libssl-dev libayatana-appindicator3-dev librsvg2-dev`). The corresponding
runtime libraries are already required by any WebKit-based browser. Windows
and macOS prerequisites follow the upstream Tauri docs; this shell is
developed and tested Linux-first.

## Notes and caveats

- BLE `requestDevice()` has no chooser dialog in the desktop shell: it scans
  for up to 6 s, matches the `namePrefix` filters (`LS`, `whalesbot`) and
  connects to the strongest match. Power on the robot before clicking
  "Run live".
- The BLE path is built on the recovered 20-byte NUS frame protocol and has
  the same device-testing status as the browser version (see TODO.md).
- Keep new identifiers free of vendor trademarks; the shell is "Whale
  Instructor", identifier `org.whaleinstructor.desktop`.
