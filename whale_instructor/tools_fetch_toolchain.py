#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch the arm-none-eabi cross toolchain (free software).

The only thing Whale Instructor needs from outside the package is
arm-none-eabi-gcc: free software (GPL), resolved live from the xPack
GitHub releases API with a pinned fallback, unpacked into the per-user
data dir where build_tc.py auto-discovers it. Everything else — the
runtime, drivers, startup, configs — ships inside the package and builds
without downloads.

Usage:
    whale-fetch-toolchain              # show toolchain status
    whale-fetch-toolchain fetch        # download + install (no-op if present)
"""
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

from whale_instructor import paths

UA = ('Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 '
      '(KHTML, like Gecko) Chrome/126.0 Safari/537.36')
CHUNK = 1 << 20

# xPack prebuilt arm-none-eabi-gcc (GPL). Resolved live via the GitHub
# API; the pinned release is the fallback when the API cannot be
# reached (verified against the API 2026-09-07; update both together).
XPACK_REPO_API = ('https://api.github.com/repos/xpack-dev-tools/'
                  'arm-none-eabi-gcc-xpack/releases/latest')
XPACK_PINNED_TAG = 'v15.2.1-1.1'
XPACK_PINNED_VERSION = '15.2.1-1.1'

_XPACK_ARCHES = {'aarch64': 'arm64', 'arm64': 'arm64',
                 'x86_64': 'x64', 'amd64': 'x64'}


def xpack_platform_tag(sys_platform=None, machine=None):
    """xPack asset platform tag for a machine ('linux-x64', 'darwin-arm64',
    'win32-x64', ...)."""
    sys_platform = sys_platform or sys.platform
    machine = (machine or platform.machine()).lower()
    arch = _XPACK_ARCHES.get(machine)
    if sys_platform.startswith('linux') and arch:
        return 'linux-' + arch
    if sys_platform == 'darwin' and arch:
        return 'darwin-' + arch
    if (sys_platform == 'win32' or os.name == 'nt') and arch == 'x64':
        return 'win32-x64'
    raise RuntimeError(f'no prebuilt xPack arm-none-eabi-gcc for '
                       f'{sys_platform}/{machine} — install gcc-arm-none-eabi '
                       'via your system package manager instead')


def xpack_archive_ext(tag):
    return '.zip' if tag.startswith('win32') else '.tar.gz'


def toolchain_dir_name(archive_name):
    """Dir name under <data_dir>/toolchains for an xPack archive (basename
    sans .tar.gz/.zip)."""
    for suffix in ('.tar.gz', '.zip'):
        if archive_name.endswith(suffix):
            return archive_name[:-len(suffix)]
    return archive_name


def resolve_toolchain():
    """(url, archive name, source) for the latest xPack arm-none-eabi-gcc
    release asset matching this platform."""
    tag = xpack_platform_tag()
    ext = xpack_archive_ext(tag)
    try:
        req = urllib.request.Request(
            XPACK_REPO_API,
            headers={'User-Agent': UA, 'Accept': 'application/vnd.github+json'})
        info = json.loads(urllib.request.urlopen(req, timeout=30).read())
        stem = ('xpack-arm-none-eabi-gcc-'
                + info['tag_name'].lstrip('v') + '-' + tag + ext)
        for asset in info['assets']:
            if asset['name'] == stem:
                return (asset['browser_download_url'], stem,
                        f'GitHub API (release {info["tag_name"]})')
        raise RuntimeError(f'asset {stem} not in latest release')
    except (urllib.error.URLError, TimeoutError, OSError, KeyError,
            ValueError, RuntimeError):
        stem = ('xpack-arm-none-eabi-gcc-' + XPACK_PINNED_VERSION
                + '-' + tag + ext)
        url = ('https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/'
               'releases/download/' + XPACK_PINNED_TAG + '/' + stem)
        return url, stem, f'pinned release {XPACK_PINNED_TAG} (GitHub API ' \
                          'unreachable)'


def fetch(url, dest_path):
    req = urllib.request.Request(url, headers={'User-Agent': UA})
    total = 0
    tmp = dest_path.with_name(dest_path.name + '.part')
    try:
        with urllib.request.urlopen(req, timeout=600) as r, open(tmp, 'wb') as f:
            while True:
                chunk = r.read(CHUNK)
                if not chunk:
                    break
                f.write(chunk)
                total += len(chunk)
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise
    tmp.rename(dest_path)
    return dest_path, total


def cmd_status():
    tdir = paths.data_dir() / 'toolchains'
    hits = sorted(tdir.glob('*/bin/arm-none-eabi-gcc*')) \
        if tdir.is_dir() else []
    if hits:
        print(f'toolchain: PRESENT ({hits[0].parent.parent.name})')
    else:
        print('toolchain: not installed')
    print(f'location:  {tdir}')
    print('arm-none-eabi-gcc (xPack prebuilt, GPL) — auto-discovered '
          'by builds; run `whale-fetch-toolchain fetch` to install')


def cmd_fetch():
    """Download + extract the xPack toolchain into <data_dir>/toolchains."""
    url, name, source = resolve_toolchain()
    print(f'   source: {source}')
    print(f'   url:    {url}')
    tdir = paths.data_dir() / 'toolchains'
    target = tdir / toolchain_dir_name(name)
    exe = target / 'bin' / ('arm-none-eabi-gcc.exe'
                            if os.name == 'nt' else 'arm-none-eabi-gcc')
    if target.is_dir():
        if exe.is_file():
            print(f'   exists, skipping: {target}')
            return 0
        print(f'   FAILED: {target} exists but {exe.name} is missing; '
              'remove it and retry')
        return 1
    tdir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='whale_tc_'))
    try:
        archive, _ = fetch(url, work / name)
        print(f'   extracting {archive.name} ...')
        out = work / 'out'
        out.mkdir()
        if archive.name.endswith('.zip'):
            with zipfile.ZipFile(archive) as z:
                z.extractall(out)
        else:
            with tarfile.open(archive) as t:
                t.extractall(out)
        roots = [p.parent.parent for p in out.glob('*/bin/' + exe.name)]
        if not roots and (out / 'bin' / exe.name).is_file():
            roots = [out]
        if not roots:
            raise RuntimeError('toolchain root (bin/' + exe.name
                               + ') not found inside the archive')
        shutil.move(str(roots[0]), str(target))
        r = subprocess.run([str(exe), '--version'], capture_output=True,
                           text=True, timeout=60)
        if r.returncode != 0:
            raise RuntimeError(exe.name + ' --version failed: '
                               + (r.stderr or r.stdout)[:500])
        print(f'   {r.stdout.splitlines()[0]}')
        print(f'   installed -> {target} (auto-discovered by build_tc)')
        return 0
    except (urllib.error.URLError, TimeoutError, OSError, RuntimeError,
            subprocess.SubprocessError) as e:
        shutil.rmtree(target, ignore_errors=True)
        print(f'   FAILED: {e}')
        return 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main(argv=None):
    argv = sys.argv if argv is None else argv
    args = [a for a in argv[1:] if a != '--']
    if not args or args[0] in ('list', 'status'):
        cmd_status()
    elif args[0] == 'fetch':
        sys.exit(cmd_fetch())
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv)
