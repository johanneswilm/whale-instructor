#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Whale Instructor — local web IDE server (stdlib only).

One pipeline for kids: Blocks/Python -> py2c -> build_tc.py -> whale_cli.py
upload. C programs can be written and built directly. Listens on 127.0.0.1
only. Every external command runs as an argv list via subprocess (never a
shell), so the same code works on Linux, macOS and Windows.

API (all JSON unless noted):
  GET  /api/ping                 liveness canary for the frontend
  GET  /api/toolchains            available arm-none-eabi-gcc's (newest first)
  GET  /api/help                  the device API reference (from py2c)
  GET  /api/probe                 detect the controller (returns model string)
  POST /api/build {name,lang,toolchain,slot,code}  transpile + compile (job)
  POST /api/upload {name,slot}    upload the built image to slot P1..P3 (job)
  POST /api/stop                  live "stop all motors" command
  POST /api/save_local {path,content}  write a file to a user-chosen path
                                  (Tauri shell; the browser build saves
                                  client-side via the File System Access API)
  POST /api/read_local {path}     read a user-chosen absolute path back
                                  (the desktop shell's launch argument)
  POST /api/transpile {name,code[,target=c|js]}  Python -> C/JS (JSON reply)
  POST /api/quit                  shut the IDE server down (from the UI)
  GET  /api/log?since=N           job log lines since N (poll while running)
  GET  /api/c?name=X              the C generated for a program
  GET  /api/debug/read?kind=ir|touch&port=N   live sensor read (JSON)
  POST /api/debug/motor {port,speed}          live motor -100..100 (JSON)

Run:  whale-ide [--port 8766]   (or python3 -m whale_instructor.serve_ide)
      whale-ide --install-launcher / --uninstall-launcher   desktop icon
"""
import argparse
import ast
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from whale_instructor import build_tc, paths, py2c, whale_cli

STATIC = paths.PACKAGE_DIR / 'static'
ARTIFACTS = (paths.REPO_ROOT or paths.data_dir()) / 'artifacts' / 'ide'

NAME_RE = re.compile(r'^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$')
MAX_BODY = 1 << 20          # 1 MiB
UPLOAD_GUARDS = ('bridge_server.py', 'Block Studio')

# Direct device access (debug panel) is serialized; uploads run as
# subprocesses and are guarded by checking for unfinished upload jobs.
DEVICE_LOCK = threading.Lock()

CONTENT_TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.json': 'application/json',
    '.svg': 'image/svg+xml',
    '.png': 'image/png',
    '.ico': 'image/x-icon',
}


class Job:
    """One background build/upload, with a polled line log."""

    def __init__(self, label):
        self.id = uuid.uuid4().hex[:12]
        self.label = label
        self.lines = []
        self.done = False
        self.ok = False
        self.result = {}

    def log(self, text):
        for ln in str(text).rstrip('\n').splitlines() or ['']:
            self.lines.append(ln)

    def finish(self, ok, result=None):
        self.ok = ok
        self.done = True
        if result:
            self.result = result


JOBS = {}                    # job id -> Job
LAST_JOB = [None]            # most recent Job (for /api/log without id)
JOB_LOCK = threading.Lock()


def start_job(label, target):
    job = Job(label)
    with JOB_LOCK:
        JOBS[job.id] = job
        LAST_JOB[0] = job
    t = threading.Thread(target=target, args=(job,), daemon=True)
    job.log(f'== {label}')
    t.start()
    return job


BUNDLE_MODULE_ENV = 'WHALE_BUNDLE_MODULE'


def module_cmd(mod, args):
    """argv (+env override) to run another whale_instructor CLI module as a
    subprocess. A frozen app has no `python -m`: the bundle executable
    re-dispatches through WHALE_BUNDLE_MODULE (handled by the generated
    PyInstaller entry script)."""
    if getattr(sys, 'frozen', False):
        env = os.environ.copy()
        env[BUNDLE_MODULE_ENV] = mod
        return [sys.executable] + list(args), env
    return [sys.executable, '-m', mod] + list(args), None


def run_streamed(job, cmd, cwd=None, env=None):
    """Run an argv-list command, streaming merged output into the job log."""
    job.log('$ ' + ' '.join(cmd))
    try:
        proc = subprocess.Popen(
            cmd, cwd=cwd, env=env, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, errors='replace')
    except OSError as e:
        job.log(f'could not start {cmd[0]}: {e}')
        return False
    assert proc.stdout is not None
    for line in proc.stdout:
        job.log(line.rstrip('\n'))
    proc.wait()
    if proc.returncode != 0:
        job.log(f'-- exit code {proc.returncode}')
        return False
    return True


def bridge_or_app_running():
    """Best-effort check for processes that fight over the HID device.

    Critical upload rule: the Wine app / bridge must NOT be running during
    whale_cli.py uploads. Check Linux /proc; on other OSes return None (the
    check is best-effort there).
    """
    procdir = Path('/proc')
    if not procdir.is_dir():
        return None
    for pid in procdir.iterdir():
        if not pid.name.isdigit():
            continue
        try:
            cmdline = (pid / 'cmdline').read_bytes().replace(b'\x00', b' ')
        except OSError:
            continue
        for guard in UPLOAD_GUARDS:
            if guard in cmdline.decode('utf-8', 'replace'):
                return guard
    return None


def upload_job_running():
    """True while any build/upload job is still running (device busy)."""
    with JOB_LOCK:
        return any(not j.done for j in JOBS.values())


# ---- job targets -----------------------------------------------------------

def transpile_source(code, name, target='c'):
    """Python source -> C or JS text, or raise ValueError with a
    line-numbered message for the IDE log."""
    try:
        tree = ast.parse(code, name + '.py')
    except SyntaxError as e:
        raise ValueError(f'{name}.py:{e.lineno}: error: {e.msg}')
    try:
        return py2c.Transpiler(tree, target=target).run_transpile()
    except py2c.TranspileError as e:
        raise ValueError(f'{name}.py:{e.lineno}: error: {e.msg}')


def transpile_program(name, code, job):
    """py2c: Python source -> user_main.c under artifacts/ide/<name>/."""
    try:
        c = transpile_source(code, name)
    except ValueError as e:
        job.log(str(e))
        job.log('fix the highlighted problem and build again')
        return None
    outdir = ARTIFACTS / name
    outdir.mkdir(parents=True, exist_ok=True)
    cfile = outdir / 'user_main.c'
    cfile.write_text(c)
    job.log(f'transpiled {name}.py -> {cfile} '
            f'({len(c.splitlines())} lines)')
    return cfile


def build_target(job, payload):
    name = payload.get('name', '')
    lang = payload.get('lang') or 'py'
    slot = int(payload.get('slot') or 1)
    toolchain = payload.get('toolchain') or ''
    code = payload.get('code')
    outdir = ARTIFACTS / name
    if lang == 'c':
        if code is None:
            job.log('no program code in the build request')
            job.finish(False)
            return
        outdir.mkdir(parents=True, exist_ok=True)
        cfile = outdir / 'user_main.c'
        cfile.write_text(code)
        job.log(f'building C program {name}.c directly '
                f'({len(code.splitlines())} lines)')
    else:
        if code is None:
            job.log('no program code in the build request')
            job.finish(False)
            return
        cfile = transpile_program(name, code, job)
        if cfile is None:
            job.finish(False)
            return
    bindir = cfile.parent
    binfile = bindir / f'APP_{slot}.bin'
    cmd, env = module_cmd('whale_instructor.build_tc',
                          [str(cfile), str(slot), str(binfile)])
    if toolchain:
        cmd.append('--gcc=' + toolchain)
    ok = run_streamed(job, cmd, cwd=str(paths.REPO_ROOT)
                      if paths.REPO_ROOT else None, env=env)
    if ok and binfile.is_file():
        job.log(f'built {binfile.name} ({binfile.stat().st_size} bytes) '
                f'for slot P{slot}')
        job.finish(True, {'bin': str(binfile), 'c': str(cfile)})
    else:
        job.finish(False)


def upload_target(job, payload):
    name = payload.get('name', '')
    slot = int(payload.get('slot') or 1)
    binfile = ARTIFACTS / name / f'APP_{slot}.bin'
    if not binfile.is_file():
        job.log(f'no image for {name!r} slot P{slot} — press Build first')
        job.finish(False)
        return
    guard = bridge_or_app_running()
    if guard:
        job.log(f'REFUSED: {guard} is running and would corrupt the upload.')
        job.log('stop it, then upload again (see the README).')
        job.finish(False)
        return
    # friendly pre-check so kids do not see a raw traceback
    try:
        pcmd, penv = module_cmd('whale_instructor.whale_cli', ['probe'])
        r = subprocess.run(pcmd, capture_output=True, text=True, timeout=8,
                           env=penv)
    except (OSError, subprocess.TimeoutExpired):
        job.log('controller did not answer: is it ON and plugged in?')
        job.finish(False)
        return
    if r.returncode != 0:
        job.log('controller not found — power it on and plug in USB.')
        job.finish(False)
        return
    job.log('controller present: ' + r.stdout.strip())
    cmd, env = module_cmd('whale_instructor.whale_cli',
                          ['upload', str(binfile), str(slot)])
    ok = run_streamed(job, cmd, cwd=str(paths.REPO_ROOT)
                      if paths.REPO_ROOT else None, env=env)
    job.finish(ok, {'slot': slot} if ok else None)


def probe_target(job):
    try:
        with DEVICE_LOCK:
            bot = whale_cli.Controller()
            bot.open()
            try:
                model = bot.probe()
            finally:
                bot.close()
    except (OSError, IOError) as e:
        job.log('controller not found — power it on and plug in USB. '
                f'({e})')
        job.finish(False)
        return
    job.log('controller found: ' + model)
    job.finish(True, {'model': model})


def stop_target(job):
    cmd, env = module_cmd('whale_instructor.whale_cli', ['motors-stop'])
    ok = run_streamed(job, cmd, cwd=str(paths.REPO_ROOT)
                      if paths.REPO_ROOT else None, env=env)
    job.log('all motors stopped' if ok else 'could not reach controller')
    job.finish(ok)


# ---- HTTP ------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = 'WhaleInstructor/0.1'

    # -- helpers -------------------------------------------------------------
    def send_json(self, obj, code=200):
        body = json.dumps(obj).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def read_json(self):
        length = int(self.headers.get('Content-Length') or 0)
        if length <= 0 or length > MAX_BODY:
            raise ValueError('bad request body size')
        return json.loads(self.rfile.read(length).decode('utf-8'))

    def log_message(self, fmt, *args):  # quiet
        pass

    # -- static --------------------------------------------------------------
    def serve_static(self, path):
        if path in ('/', '/index.html'):
            path = '/index.html'
        rel = path[len('/static/'):] if path.startswith('/static/') \
            else path.lstrip('/')
        f = (STATIC / rel).resolve()
        if not f.is_file() or STATIC.resolve() not in f.parents:
            self.send_error(404)
            return
        data = f.read_bytes()
        ctype = CONTENT_TYPES.get(f.suffix, 'application/octet-stream')
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    # -- GET -----------------------------------------------------------------
    def do_GET(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        try:
            if url.path == '/' or not url.path.startswith('/api/'):
                self.serve_static(url.path)
                return
            if url.path == '/api/ping':
                self.send_json({'ok': True})
            elif url.path == '/api/toolchains':
                tcs = build_tc.discover_toolchains()
                self.send_json(
                    [{'path': t, 'label': self.tc_label(t)} for t in tcs])
            elif url.path == '/api/help':
                self.send_json(py2c.api_reference())
            elif url.path == '/api/probe':
                job = start_job('probe controller', probe_target)
                self.send_json({'job': job.id})
            elif url.path == '/api/log':
                with JOB_LOCK:
                    job = JOBS.get((q.get('job') or [None])[0]) \
                        or LAST_JOB[0]
                if job is None:
                    self.send_json({'lines': [], 'done': True, 'ok': None})
                    return
                since = int((q.get('since') or [0])[0])
                self.send_json({'id': job.id, 'label': job.label,
                                'lines': job.lines[since:],
                                'count': len(job.lines), 'done': job.done,
                                'ok': job.ok, 'result': job.result})
            elif url.path == '/api/c':
                name = q['name'][0]
                if not NAME_RE.match(name):
                    raise ValueError('invalid name')
                f = (ARTIFACTS / name / 'user_main.c')
                if not f.is_file():
                    self.send_json({'c': None})
                else:
                    self.send_json({'c': f.read_text()})
            elif url.path == '/api/debug/read':
                kind = (q.get('kind') or [''])[0]
                port = int((q.get('port') or [0])[0])
                if upload_job_running():
                    raise ValueError('a build/upload is using the '
                                     'controller — try again after it '
                                     'finishes')
                with DEVICE_LOCK:
                    bot = whale_cli.Controller()
                    bot.open()
                    try:
                        value = bot.read_sensor(kind, port)
                    finally:
                        bot.close()
                self.send_json({'kind': kind, 'port': port, 'value': value})
            else:
                self.send_json({'error': 'unknown endpoint'}, 404)
        except (ValueError, KeyError, OSError, IOError) as e:
            self.send_json({'error': str(e)}, 400)

    @staticmethod
    def tc_label(path):
        m = re.search(r'(xpack-[\d.]+|gcc-arm-none-eabi-[\d.-]+|arm-gnu)',
                      path)
        label = m.group(1) if m else Path(path).parent.name
        try:
            label += f' (gcc {build_tc.gcc_major(path)})'
        except (ValueError, OSError, subprocess.SubprocessError):
            pass
        return label

    # -- POST ----------------------------------------------------------------
    def do_POST(self):
        url = urlparse(self.path)
        if url.path == '/api/quit':
            # tolerate any body (or none): this branch runs before read_json
            self.send_json({'quit': True})
            threading.Thread(target=self.server.shutdown,
                             daemon=True).start()
            return
        try:
            payload = self.read_json()
            if url.path == '/api/read_local':
                # read a user-chosen absolute path (the desktop shell's
                # launch argument); mirrors /api/save_local
                path = payload.get('path', '')
                if not path:
                    raise ValueError('no path')
                p = Path(path).expanduser()
                if not p.is_absolute():
                    raise ValueError('path must be absolute')
                if not p.is_file():
                    raise ValueError(f'no such file: {p}')
                lang = 'c' if p.suffix.lower() == '.c' else 'py'
                self.send_json({'name': p.stem, 'lang': lang,
                                'content': p.read_text()})
            elif url.path == '/api/build':
                job = start_job('build ' + payload.get('name', '?'),
                                lambda j: build_target(j, payload))
                self.send_json({'job': job.id})
            elif url.path == '/api/upload':
                job = start_job('upload ' + payload.get('name', '?'),
                                lambda j: upload_target(j, payload))
                self.send_json({'job': job.id})
            elif url.path == '/api/stop':
                job = start_job('stop motors', stop_target)
                self.send_json({'job': job.id})
            elif url.path == '/api/transpile':
                # fast, synchronous: Python -> C or JS without compiling;
                # used by the C tab's "Take C from Python" button and by
                # "Run live" (target=js)
                name = payload.get('name') or 'program'
                if not NAME_RE.match(name):
                    raise ValueError('invalid program name')
                target = payload.get('target') or 'c'
                if target not in ('c', 'js'):
                    raise ValueError('invalid target')
                key = 'js' if target == 'js' else 'c'
                self.send_json(
                    {key: transpile_source(payload.get('code', ''), name,
                                           target)})
            elif url.path == '/api/save_local':
                # write to a user-chosen absolute path (the Tauri shell's
                # native save dialog returns it); the browser build uses
                # the File System Access API instead and never calls this
                path = payload.get('path', '')
                if not path:
                    raise ValueError('no path')
                p = Path(path).expanduser()
                if not p.is_absolute():
                    raise ValueError('path must be absolute')
                if p.is_dir():
                    raise ValueError('path is a directory')
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(payload.get('content', ''))
                self.send_json({'saved': str(p)})
            elif url.path == '/api/debug/motor':
                port = payload.get('port', 'A')
                speed = int(payload.get('speed') or 0)
                if upload_job_running():
                    raise ValueError('a build/upload is using the '
                                     'controller — try again after it '
                                     'finishes')
                with DEVICE_LOCK:
                    bot = whale_cli.Controller()
                    bot.open()
                    try:
                        bot.motor(port, speed)
                    finally:
                        bot.close()
                self.send_json({'port': port, 'speed': speed})
            else:
                self.send_json({'error': 'unknown endpoint'}, 404)
        except (ValueError, KeyError, OSError, IOError,
                json.JSONDecodeError) as e:
            self.send_json({'error': str(e)}, 400)


# ---- desktop launcher ------------------------------------------------------

DESKTOP_FILE = '''[Desktop Entry]
Type=Application
Name=Whale Instructor
Exec=sh -lc "whale-ide"
Terminal=false
Categories=Education;
'''


def desktop_file_content():
    """Freedesktop entry; sh -lc so a pipx-installed whale-ide resolves."""
    return DESKTOP_FILE


def windows_cmd_content(whale_ide):
    return f'@echo off\r\n"{whale_ide}" %*\r\n'


def macos_info_plist():
    import plistlib
    return plistlib.dumps({
        'CFBundleName': 'Whale Instructor',
        'CFBundleExecutable': 'whale-instructor',
        'CFBundleIdentifier': 'org.whaleinstructor.ide',
        'CFBundleVersion': '1.0',
    })


def macos_launcher_script(whale_ide):
    return f'#!/bin/sh\nexec "{whale_ide}" "$@"\n'


def launcher_target(sys_platform, home, appdata=None):
    """Where the launcher lives on this platform (home/appdata injectable
    for tests)."""
    home = Path(home)
    if sys_platform.startswith('linux'):
        return home / '.local/share/applications/whale-instructor.desktop'
    if sys_platform == 'darwin':
        return home / 'Applications/Whale Instructor.app'
    return Path(appdata) / ('Microsoft/Windows/Start Menu/Programs/'
                            'Whale Instructor.cmd')


def _resolved_whale_ide():
    ide = shutil.which('whale-ide')
    if not ide:
        sys.exit('whale-ide not found on PATH — install it first '
                 '(e.g. "pipx install whale-instructor")')
    return ide


def install_launcher(sys_platform=None, home=None, appdata=None):
    plat = sys_platform or sys.platform
    target = launcher_target(plat, home or Path.home(), appdata)
    if plat.startswith('linux'):
        if target.exists():
            print(f'already installed: {target}')
            return 0
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(desktop_file_content())
        print(f'created {target}')
    elif plat == 'darwin':
        if target.is_dir():
            print(f'already installed: {target}')
            return 0
        ide = _resolved_whale_ide()
        macos = target / 'Contents/MacOS'
        macos.mkdir(parents=True)
        (target / 'Contents/Info.plist').write_bytes(macos_info_plist())
        script = macos / 'whale-instructor'
        script.write_text(macos_launcher_script(ide))
        script.chmod(0o755)
        print(f'created {target}')
    else:
        if target.exists():
            print(f'already installed: {target}')
            return 0
        ide = _resolved_whale_ide()
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(windows_cmd_content(ide))
        print(f'created {target}')
    return 0


def uninstall_launcher(sys_platform=None, home=None, appdata=None):
    plat = sys_platform or sys.platform
    target = launcher_target(plat, home or Path.home(), appdata)
    if plat == 'darwin':
        if not target.is_dir():
            print(f'not installed: {target}')
            return 0
        shutil.rmtree(target)
    else:
        if not target.exists():
            print(f'not installed: {target}')
            return 0
        target.unlink()
    print(f'removed {target}')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--port', type=int, default=8766)
    ap.add_argument('--no-open', action='store_true',
                    help='do not open a browser window')
    ap.add_argument('--install-launcher', action='store_true',
                    help='create a desktop entry that starts whale-ide, '
                         'then exit')
    ap.add_argument('--uninstall-launcher', action='store_true',
                    help='remove the desktop entry, then exit')
    args = ap.parse_args(argv)
    if args.install_launcher:
        return install_launcher()
    if args.uninstall_launcher:
        return uninstall_launcher()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    url = f'http://127.0.0.1:{args.port}/'
    print(f'Whale Instructor running at {url}  (Ctrl+C to quit)')
    if not args.no_open:
        try:
            import webbrowser
            webbrowser.open(url)
        except OSError:
            pass
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nbye')
    finally:
        server.server_close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
