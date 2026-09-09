#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the publishable staging tree (publish/) from this research repo.

The working repo holds private research material (gitignored `attic/`,
`reference/`, `vendor/`; see attic/AGENTS.md). NONE of that may reach
GitHub. This script copies a strict WHITELIST into publish/ and then runs
safety checks over the result.

Usage:
  python3 make_publish.py            # build publish/
  python3 make_publish.py --check    # re-run checks on an existing tree
"""
import argparse
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent
PUBLISH = REPO / 'publish'

# Files/dirs copied verbatim. Keep this list SHORT: anything not listed
# here stays private, by design.
WHITELIST_FILES = [
    'LICENSE',
    'LICENSE_EXCEPTION.md',
    'README.md',
    'AGENTS.md',
    'PUBLISHING.md',
    'CONTRIBUTING.md',
    'THIRD_PARTY_NOTICES.md',
    'TODO.md',
    'pyproject.toml',
    'MANIFEST.in',
    'tools_build_bundle.py',
]
WHITELIST_DIRS = [
    'tests',
    'whale_instructor',
]

# Patterns that must NEVER appear anywhere in publish/ (device images and
# vendor objects inside them; Windows/vendor binaries; private trees).
FORBIDDEN_SUFFIXES = ('.bin', '.dll', '.exe', '.a', '.pdb', '.deb',
                      '.lib', '.o', '.elf')
FORBIDDEN_NAMES = ('attic', 'reference', 'vendor', 'captures',
                   'symbols.txt', 'serial_override.txt', 'strace')
FORBIDDEN_DIR_PARTS = ('__pycache__',)

MAX_FILE_BYTES = 1 << 20     # no single source file should exceed 1 MiB
# small binary assets shipped on purpose (icons); extend deliberately
IMAGE_ASSETS = {'whale_instructor/static/favicon.png'}


def copy_tree():
    if PUBLISH.exists():
        shutil.rmtree(PUBLISH)
    PUBLISH.mkdir()
    # regenerate the browser transpiler's API snapshot so the checked and
    # published copies can never drift from py2c.py's tables
    sys.path.insert(0, str(REPO))
    from whale_instructor import py2c
    py2c.write_api_json(REPO / 'whale_instructor' / 'static' / 'api.json')
    for f in WHITELIST_FILES:
        src = REPO / f
        if not src.is_file():
            sys.exit(f'whitelist file missing: {f}')
        dst = PUBLISH / f
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    for d in WHITELIST_DIRS:
        src = REPO / d
        if not src.is_dir():
            (PUBLISH / d).mkdir(parents=True)
            continue
        for p in src.rglob('*'):
            rel = p.relative_to(src)
            dst = PUBLISH / d / rel
            if any(part in FORBIDDEN_DIR_PARTS for part in p.parts):
                continue
            if p.is_dir():
                dst.mkdir(parents=True, exist_ok=True)
                continue
            if p.suffix.lower() in FORBIDDEN_SUFFIXES:
                continue    # e.g. libwhale_open.a is built by the user
            if p.suffix == '.pyc':
                continue
            if p.name == 'License.doc':
                continue    # binary Word doc; license linked in README
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, dst)
    # keep empty whitelisted dirs visible in git
    for p in sorted(PUBLISH.rglob('*')):
        if p.is_dir() and not any(p.iterdir()):
            (p / '.gitkeep').write_text('')


def check_tree():
    problems = []
    files = [p for p in PUBLISH.rglob('*') if p.is_file()]
    for p in files:
        rel = p.relative_to(PUBLISH).as_posix()
        if p.suffix.lower() in FORBIDDEN_SUFFIXES:
            problems.append(f'forbidden suffix: {rel}')
        if p.name in FORBIDDEN_NAMES:
            problems.append(f'forbidden name: {rel}')
        if p.relative_to(PUBLISH).parts[0] in FORBIDDEN_NAMES:
            problems.append(f'forbidden top-level entry: {rel}')
        if p.stat().st_size > MAX_FILE_BYTES:
            problems.append(f'too large: {rel} '
                            f'({p.stat().st_size} bytes)')
        data = p.read_bytes()
        if b'\x00' in data[:4096] and rel not in IMAGE_ASSETS:
            problems.append(f'binary file: {rel}')
        if b'MercuryController' in p.name.encode():
            problems.append(f'vendor lib reference: {rel}')
    # our source files must carry SPDX; vendored/user files are exempt
    for p in files:
        rel = p.relative_to(PUBLISH).as_posix()
        if p.suffix not in ('.py', '.js', '.c', '.h') or '/vendor/' in rel:
            continue
        if rel.startswith(('programs/', 'whale_instructor/progs/',
                           'whale_instructor/runtime/open/')):
            continue    # user data / upstream sources with own licenses
        head = p.read_text(errors='replace')[:2000]
        if 'SPDX-License-Identifier' not in head:
            problems.append(f'missing SPDX: {rel}')
    # log-like files
    for p in files:
        if p.suffix == '.log':
            problems.append(f'log file in tree: {p.relative_to(PUBLISH)}')
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--check', action='store_true',
                    help='only run checks on an existing publish/ tree')
    args = ap.parse_args(argv)
    if not args.check:
        copy_tree()
        print(f'staged {PUBLISH}')
    problems = check_tree()
    if problems:
        print('CHECKS FAILED — do not publish:')
        for p in problems:
            print('  -', p)
        return 1
    n = sum(1 for p in PUBLISH.rglob('*') if p.is_file())
    print(f'checks OK: {n} files in publish/, nothing forbidden found')
    return 0


if __name__ == '__main__':
    sys.exit(main())
