// SPDX-FileCopyrightText: Johannes Wilm
// SPDX-License-Identifier: GPL-3.0-or-later

//! Tauri v2 desktop shell for Whale Instructor.
//!
//! The shell owns the Python backend lifecycle: it spawns
//! `whale-instructor serve` (bundled sidecar, PATH install, or module
//! fallback) on a free localhost port, waits until the HTTP server answers,
//! then shows a window that loads the backend-served frontend — so the app
//! behaves exactly like the browser version (static files, JSON API, all of
//! it) with Bluetooth added through a WebBluetooth-compatible shim over
//! tauri-plugin-blec (see bridge/ble-shim.js, injected below).

use std::io::Write;
use std::net::TcpListener;
use std::net::TcpStream;
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use tauri::{Manager, RunEvent, WebviewUrl, WebviewWindowBuilder};

const PORT_START: u16 = 18765;
const PORT_TRIES: u16 = 20;
const BACKEND_READY_TIMEOUT: Duration = Duration::from_secs(20);

const BLE_SHIM: &str = include_str!("../bridge/ble-shim.js");

struct BackendState {
    child: Mutex<Option<Child>>,
    port: u16,
}

/// Rust target triple of this binary, mirroring Tauri's externalBin naming
/// (sidecars live at `binaries/whale-instructor-<triple>`).
fn current_target_triple() -> String {
    let arch = std::env::consts::ARCH;
    let os = std::env::consts::OS;
    let vendor_env = match os {
        "windows" => "pc-windows-msvc",
        "macos" => "apple-darwin",
        _ => "unknown-linux-gnu",
    };
    format!("{}-{}", arch, vendor_env)
}

/// First free port in 18765..18765+20.
fn pick_port() -> Option<u16> {
    (PORT_START..PORT_START + PORT_TRIES)
        .find(|p| TcpListener::bind(("127.0.0.1", *p)).is_ok())
}

/// Candidate commands for the backend, in priority order.
///
/// Release (the shipped, self-contained app):
/// 1. $WHALE_INSTRUCTOR_BIN (explicit override)
/// 2. bundled sidecar per the Tauri externalBin convention
/// 3. `whale-instructor` / `whale-ide` on PATH
/// 4. `python3 -m whale_instructor.serve_ide`
///
/// Debug (development from a checkout): the sidecar comes LAST, because a
/// previously bundled PyInstaller binary carries stale baked-in static files
/// while `whale_instructor/static/` may have moved on:
/// 1. $WHALE_INSTRUCTOR_BIN (explicit override)
/// 2. `python3 -m whale_instructor.serve_ide` (repo checkout or installed
///    package — always current; the child gets PYTHONPATH pointed at the
///    nearest checkout so a bare repo tree works)
/// 3. `whale-instructor` / `whale-ide` on PATH
/// 4. bundled sidecar
/// Walk up from the compile-time manifest dir to the nearest directory that
/// is a Whale Instructor checkout (contains the `whale_instructor` package).
fn repo_with_package() -> Option<PathBuf> {
    let mut dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    loop {
        if dir.join("whale_instructor").join("__init__.py").is_file() {
            return Some(dir);
        }
        if !dir.pop() {
            return None;
        }
    }
}

fn backend_commands(app: &tauri::AppHandle) -> Vec<Command> {
    let mut cmds: Vec<Command> = Vec::new();

    if let Ok(bin) = std::env::var("WHALE_INSTRUCTOR_BIN") {
        if !bin.is_empty() {
            cmds.push(Command::new(bin));
        }
    }

    let sidecar_name = format!("whale-instructor-{}", current_target_triple());
    let mut sidecar_dirs: Vec<PathBuf> = Vec::new();
    if let Ok(res) = app.path().resource_dir() {
        sidecar_dirs.push(res.join("binaries"));
    }
    // `cargo run` / plain cargo builds never copy the sidecar next to the
    // binary; also look straight into src-tauri/binaries (CARGO_MANIFEST_DIR
    // is src-tauri at compile time).
    sidecar_dirs.push(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("binaries"));
    let mut sidecar: Option<PathBuf> = None;
    for dir in sidecar_dirs {
        let p = dir.join(&sidecar_name);
        if p.is_file() {
            sidecar = Some(p);
            break;
        }
    }

    let mut python = Command::new("python3");
    python.args(["-m", "whale_instructor.serve_ide"]);
    if cfg!(debug_assertions) {
        // The child gets PYTHONPATH pointed at the nearest checkout so a
        // bare repo tree works without installing the package.
        if let Some(root) = repo_with_package() {
            let mut pythonpath = root.display().to_string();
            if let Ok(existing) = std::env::var("PYTHONPATH") {
                if !existing.is_empty() {
                    pythonpath.push(':');
                    pythonpath.push_str(&existing);
                }
            }
            python.env("PYTHONPATH", pythonpath);
        }
    }

    let mut path_cmds: Vec<Command> = ["whale-instructor", "whale-ide"]
        .into_iter()
        .map(Command::new)
        .collect();

    if cfg!(debug_assertions) {
        cmds.push(python);
        cmds.append(&mut path_cmds);
        if let Some(p) = sidecar {
            cmds.push(Command::new(p));
        }
    } else {
        if let Some(p) = sidecar {
            cmds.push(Command::new(p));
        }
        cmds.append(&mut path_cmds);
        cmds.push(python);
    }

    cmds
}

/// Spawn backend candidates in order until one stays up and answers. A
/// candidate that fails to spawn or exits before the port answers is
/// discarded and the next one is tried.
fn spawn_backend(app: &tauri::AppHandle, port: u16) -> Option<Child> {
    let deadline = Instant::now() + BACKEND_READY_TIMEOUT;
    // The backend stores its programs/ under the CWD, so give it a stable
    // per-user dir: never the src-tauri tree (tauri dev's file watcher
    // would restart the whole app on every program save).
    let workdir = app
        .path()
        .app_data_dir()
        .unwrap_or_else(|_| std::env::temp_dir());
    if let Err(e) = std::fs::create_dir_all(&workdir) {
        eprintln!("[desktop] cannot create backend workdir {workdir:?}: {e}");
    }
    for mut cmd in backend_commands(app) {
        cmd.args(["--no-open", "--port", &port.to_string()])
            .current_dir(&workdir)
            .stdin(Stdio::null())
            .stdout(Stdio::inherit())
            .stderr(Stdio::inherit());
        let mut child = match cmd.spawn() {
            Ok(child) => child,
            Err(e) => {
                eprintln!("[desktop] backend candidate {cmd:?} failed: {e}");
                continue;
            }
        };
        eprintln!("[desktop] started backend candidate on 127.0.0.1:{port}: {cmd:?}");
        let remaining = deadline.saturating_duration_since(Instant::now());
        let slice = remaining.min(Duration::from_secs(6));
        if !slice.is_zero() && wait_for_backend(&mut child, port, slice) {
            return Some(child);
        }
        let _ = child.kill();
        let _ = child.wait();
        if Instant::now() >= deadline {
            break;
        }
    }
    eprintln!("[desktop] no backend candidate answered on 127.0.0.1:{port}");
    None
}

/// Poll the backend URL (or notice the child dying) until it answers.
fn wait_for_backend(child: &mut Child, port: u16, timeout: Duration) -> bool {
    let deadline = Instant::now() + timeout;
    loop {
        if TcpStream::connect(("127.0.0.1", port)).is_ok() {
            return true;
        }
        match child.try_wait() {
            Ok(Some(status)) => {
                eprintln!("[desktop] backend exited early with {status}");
                return false;
            }
            Ok(None) => {}
            Err(e) => eprintln!("[desktop] backend wait error: {e}"),
        }
        if Instant::now() >= deadline {
            return false;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

fn backend_url(port: u16, file: Option<&std::path::Path>) -> String {
    let mut url = format!("http://127.0.0.1:{port}/");
    if let Some(f) = file {
        url.push_str("?file=");
        url.push_str(&query_encode(&f.to_string_lossy()));
    }
    url
}

/// A program file to open straight away, taken from the command line:
/// `whale-instructor <file.py|file.c>`. Flag-looking arguments are
/// skipped; the first existing .py/.c file wins.
fn launch_program() -> Option<PathBuf> {
    for arg in std::env::args().skip(1) {
        if arg.starts_with('-') {
            continue;
        }
        let p = PathBuf::from(&arg);
        let ext = p
            .extension()
            .and_then(|e| e.to_str())
            .map(|e| e.to_ascii_lowercase());
        if ext.as_deref() != Some("py") && ext.as_deref() != Some("c") {
            continue;
        }
        return match std::fs::canonicalize(&p) {
            Ok(abs) => Some(abs),
            Err(e) => {
                eprintln!("[desktop] cannot open {arg}: {e}");
                None
            }
        };
    }
    None
}

/// Percent-encode a path for a URL query value; unreserved bytes and
/// '/' stay literal so the frontend's URLSearchParams decodes the rest.
fn query_encode(s: &str) -> String {
    let mut out = String::new();
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9'
            | b'-' | b'_' | b'.' | b'~' | b'/' => out.push(b as char),
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

/// POST an empty body to /api/quit and give the server a moment to stop.
fn request_backend_quit(port: u16) {
    if let Ok(mut stream) = TcpStream::connect(("127.0.0.1", port)) {
        let _ = stream.write_all(
            b"POST /api/quit HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
        );
        let _ = stream.flush();
    }
    std::thread::sleep(Duration::from_millis(800));
}

fn kill_backend(state: &BackendState) {
    let mut guard = state.child.lock().unwrap();
    if let Some(mut child) = guard.take() {
        if child.try_wait().ok().flatten().is_none() {
            let _ = child.kill();
            eprintln!("[desktop] killed lingering backend");
        }
        let _ = child.wait();
    }
}

fn main() {
    let app = tauri::Builder::default()
        .plugin(tauri_plugin_blec::init())
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            let port = pick_port()
                .unwrap_or_else(|| panic!("no free port in {PORT_START}..{}", PORT_START + PORT_TRIES));
            let child = spawn_backend(&app.handle(), port);
            let state = Arc::new(BackendState {
                child: Mutex::new(child),
                port,
            });

            // Splash: create the window VISIBLE right away, loading the
            // local placeholder page. Upstream Wayland bug (tauri#11856):
            // a window that is created hidden and .show()n later gets dead
            // titlebar buttons until it is maximized — so the window must
            // never be hidden. The placeholder navigates to the backend
            // once it answers. The window title is final from creation:
            // set_title does not reliably update the Wayland header bar
            // (tauri#13749), so the "starting" state lives in the splash
            // page, not the window title.
            let window_icon = tauri::image::Image::from_bytes(
                include_bytes!("../icons/icon.png"));
            let mut window_builder =
                WebviewWindowBuilder::new(app, "main", WebviewUrl::App("index.html".into()));
            if let Ok(icon) = window_icon {
                window_builder = window_builder.icon(icon).expect("window icon");
            }
            let window = window_builder
                .title("Whale Instructor")
                .inner_size(1200.0, 800.0)
                .min_inner_size(800.0, 600.0)
                .initialization_script(BLE_SHIM)
                .build()?;
            // Workaround for the same Wayland decoration bug for every
            // hide/show cycle (e.g. minimize to tray later): toggling
            // resizable on focus revives the titlebar buttons.
            #[cfg(target_os = "linux")]
            {
                let win = window.clone();
                window.on_window_event(move |event| {
                    if let tauri::WindowEvent::Focused(true) = event {
                        let _ = win.set_resizable(false);
                        let _ = win.set_resizable(true);
                    }
                });
            }

            let state_wait = state.clone();
            let handle = app.handle().clone();

            if std::env::var_os("WHALE_BLE_SELFTEST").is_some() {
                std::thread::spawn(|| {
                    std::thread::sleep(std::time::Duration::from_secs(2));
                    eprintln!("[selftest] scanning via plugin handler…");
                    let handler = tauri_plugin_blec::get_handler().unwrap();
                    let (tx, mut rx) = tauri::async_runtime::channel(1);
                    tauri::async_runtime::spawn(async move {
                        let r = handler
                            .discover(
                                Some(tx),
                                6000,
                                tauri_plugin_blec::models::ScanFilter::None,
                                false,
                            )
                            .await;
                        eprintln!("[selftest] discover returned: {r:?}");
                    });
                    tauri::async_runtime::block_on(async move {
                        while let Some(devs) = rx.recv().await {
                            for d in &devs {
                                eprintln!("[selftest] {} {:?} {:?}", d.address, d.name, d.rssi);
                            }
                        }
                    });
                    eprintln!("[selftest] connecting…");
                    let connect = tauri::async_runtime::spawn(async move {
                        let r = handler
                            .connect(
                                "ED:67:3B:10:6B:56",
                                tauri_plugin_blec::OnDisconnectHandler::None,
                                false,
                            )
                            .await;
                        eprintln!("[selftest] connect returned: {r:?}");
                        r
                    });
                    let ok = tauri::async_runtime::block_on(connect).unwrap();
                    if ok.is_ok() {
                        use std::str::FromStr;
                        use uuid::Uuid;
                        let rx = Uuid::from_str(
                            "6e400003-b5a3-f393-e0a9-e50e24dcca9e").unwrap();
                        let subs = tauri::async_runtime::block_on(
                            handler.subscribe(rx, None, |_: Vec<u8>| {}));
                        eprintln!("[selftest] subscribe returned: {subs:?}");
                        let r2 = tauri::async_runtime::block_on(handler.disconnect());
                        eprintln!("[selftest] disconnect returned: {r2:?}");
                    }
                    eprintln!("[selftest] done");
                    std::process::exit(0);
                });
            }
            let launch = launch_program();
            std::thread::spawn(move || {
                let mut child_guard = match state_wait.child.lock() {
                    Ok(g) => g,
                    Err(e) => {
                        eprintln!("[desktop] backend lock poisoned: {e}");
                        return;
                    }
                };
                let ready = match child_guard.as_mut() {
                    Some(child) => wait_for_backend(child, state_wait.port, BACKEND_READY_TIMEOUT),
                    None => false,
                };
                drop(child_guard);

                let Some(window) = handle.get_webview_window("main") else {
                    return;
                };
                if ready {
                    let _ = window.navigate(
                        backend_url(state_wait.port, launch.as_deref())
                            .parse()
                            .expect("valid backend url"),
                    );
                    // Watch for the backend dying under us (polled so the
                    // child handle stays in shared state and ExitRequested
                    // can still kill it).
                    let state_watch = state_wait.clone();
                    let handle = handle.clone();
                    std::thread::spawn(move || loop {
                        let mut child_guard = state_watch.child.lock().unwrap();
                        let mut exited = None;
                        if let Some(child) = child_guard.as_mut() {
                            match child.try_wait() {
                                Ok(Some(status)) => exited = Some(format!("{status}")),
                                Ok(None) => {}
                                Err(e) => exited = Some(format!("wait error: {e}")),
                            }
                        }
                        if let Some(reason) = exited {
                            eprintln!("[desktop] backend exited on its own: {reason}");
                            *child_guard = None;
                            drop(child_guard);
                            if let Some(window) = handle.get_webview_window("main") {
                                let _ = window
                                    .set_title("Whale Instructor — backend stopped");
                            }
                            break;
                        }
                        drop(child_guard);
                        std::thread::sleep(Duration::from_millis(500));
                    });
                } else {
                    eprintln!("[desktop] backend did not start; showing placeholder page");
                }
            });

            app.manage(state);
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("error while building tauri application");

    // Note: the .setup() hook runs on RunEvent::Ready (inside run()), so the
    // managed state only exists from here on — fetch it inside the callback.
    app.run(move |app_handle, event| match event {
        RunEvent::ExitRequested { .. } => {
            if let Some(state) = app_handle.try_state::<Arc<BackendState>>() {
                request_backend_quit(state.port);
                kill_backend(state.inner());
            }
        }
        RunEvent::Exit => {
            if let Some(state) = app_handle.try_state::<Arc<BackendState>>() {
                kill_backend(state.inner());
            }
        }
        _ => {}
    });
}
