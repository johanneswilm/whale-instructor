#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble a Debian package of Whale Instructor from a wheel or the repo.

Layout (arch: all — everything is stdlib Python + static assets):
  /usr/lib/python3/dist-packages/whale_instructor/...   the package
  /usr/bin/whale, whale-ide, whale-fetch-toolchain      entry-point scripts
  /usr/share/applications/org.whaleinstructor.desktop.desktop
  /usr/share/icons/hicolor/*/apps/org.whaleinstructor.desktop.png
  /usr/share/metainfo/org.whaleinstructor.desktop.metainfo.xml

The desktop entry wraps `whale-ide` so it works from a terminal-free
launch; the metainfo (appstream) makes it show up in software centers.

Usage:
  python3 tools_make_deb.py --wheel dist/whale_instructor-*.whl -o dist/
  python3 -m whale_instructor.tools_make_deb -o dist/
"""
import argparse
import datetime
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
DESKTOP_ID = 'org.whaleinstructor.desktop'
DEB_VERSION_RE = re.compile(r'whale_instructor-([^-]+)-[^-]+$')

DESKTOP_FILE = f"""[Desktop Entry]
Type=Application
Name=Whale Instructor
GenericName=Robot programming IDE
Comment=Program WhalesBot MC101s (E7 Pro / AI S1) controllers
Exec=whale-ide
Icon={DESKTOP_ID}
Terminal=false
Categories=Education;Science;Development;IDE;
Keywords=robot;blocks;python;stem;
StartupWMClass=whale-instructor
"""

METINFO_TEMPLATE = """<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>ORGID</id>
  <metadata_license>FSFAP</metadata_license>
  <project_license>GPL-3.0-or-later</project_license>
  <name>Whale Instructor</name>
  <summary>Program WhalesBot MC101s (E7 Pro / AI S1) robots</summary>
  <description>
    <p>Whale Instructor is an open-source management suite for WhalesBot
    MC101s robot controllers (sold as E7 Pro / AI S1): a visual Blocks
    editor that grows into real Python and real C, a Python-to-C
    transpiler, a build pipeline for any modern arm-none-eabi toolchain,
    a USB uploader, a live debug panel, live programming over Bluetooth,
    and a searchable device-API reference.</p>
    <p>Programs you write stay yours: the runtime your program is built
    against is LGPL, your own code carries no license obligations.</p>
  </description>
  <launchable type="desktop-id">ORGID.desktop</launchable>
  <url type="homepage">https://github.com/johanneswilm/whale-instructor</url>
  <url type="bugtracker">https://github.com/johanneswilm/whale-instructor/issues</url>
  <developer_name>Johannes Wilm</developer_name>
  <content_rating type="oars-1.1"/>
  <releases>
    <release version="VERSION" date="DATE">
      <description><p>First public release.</p></description>
    </release>
  </releases>
  <provides>
    <binary>whale</binary>
    <binary>whale-ide</binary>
    <binary>whale-fetch-toolchain</binary>
  </provides>
</component>
"""

SHIMS = {
    'whale': 'whale_cli',
    'whale-ide': 'serve_ide',
    'whale-fetch-toolchain': 'tools_fetch_toolchain',
}


def deb_version(version):
    """Strip anything dpkg would reject (pre-release tags sort with ~)."""
    v = re.sub(r'[^0-9A-Za-z.+~-]', '', str(version))
    return v.replace('-', '~') or '0.0.0'


def write_shims(root):
    """Plain console-script shims (they only import whale_instructor)."""
    bin_dir = root / 'usr' / 'bin'
    bin_dir.mkdir(parents=True, exist_ok=True)
    for name, mod in SHIMS.items():
        p = bin_dir / name
        p.write_text(
            '#!/usr/bin/env python3\n'
            'import sys\n'
            'from whale_instructor import ' + mod + '\n'
            'sys.exit(' + mod + '.main(sys.argv))\n')
        p.chmod(0o755)


def write_icons(root, pkg_src):
    """hicolor PNGs from the package SVG (needs rsvg-convert; falls back
    to the shipped 32px favicon, upscaled by the desktop environment)."""
    svg = pkg_src / 'static' / 'favicon.svg'
    apps = root / 'usr' / 'share' / 'icons' / 'hicolor'
    for subdir, px in (('16x16', 16), ('32x32', 32), ('48x48', 48),
                       ('128x128', 128), ('256x256', 256), ('512x512', 512)):
        d = apps / subdir / 'apps'
        d.mkdir(parents=True, exist_ok=True)
        out = d / (DESKTOP_ID + '.png')
        if svg.is_file() and shutil.which('rsvg-convert'):
            subprocess.run(['rsvg-convert', '-w', str(px), '-h', str(px),
                            str(svg), '-o', str(out)], check=True)
        else:
            shutil.copyfile(pkg_src / 'static' / 'favicon.png', out)


def build_deb(pkg_src, version, out_dir):
    work = Path(tempfile.mkdtemp(prefix='whale_deb_'))
    root = work / 'pkg'
    site = root / 'usr' / 'lib' / 'python3' / 'dist-packages'
    site.mkdir(parents=True)
    (root / 'usr' / 'share' / 'doc' / 'whale-instructor').mkdir(parents=True)
    (root / 'usr' / 'share' / 'applications').mkdir(parents=True)
    (root / 'usr' / 'share' / 'metainfo').mkdir(parents=True)
    (root / 'DEBIAN').mkdir()

    shutil.copytree(pkg_src, site / 'whale_instructor',
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    write_shims(root)
    write_icons(root, pkg_src)

    (root / 'usr' / 'share' / 'applications'
     / (DESKTOP_ID + '.desktop')).write_text(DESKTOP_FILE)
    (root / 'usr' / 'share' / 'metainfo'
     / (DESKTOP_ID + '.metainfo.xml')).write_text(
        METINFO_TEMPLATE.replace('ORGID', DESKTOP_ID).replace(
            'VERSION', str(version)).replace(
            'DATE', datetime.date.today().isoformat()))

    size = int(subprocess.run(['du', '-sk', str(root / 'usr')],
                              capture_output=True, text=True, check=True
                              ).stdout.split()[0])
    (root / 'DEBIAN' / 'control').write_text(
        'Package: whale-instructor\n'
        f'Version: {deb_version(version)}\n'
        'Architecture: all\n'
        'Maintainer: Johannes Wilm <mail@johanneswilm.org>\n'
        f'Installed-Size: {size}\n'
        'Depends: python3 (>= 3.9), python3:any\n'
        'Suggests: gcc-arm-none-eabi\n'
        'Section: devel\n'
        'Priority: optional\n'
        'Homepage: https://github.com/johanneswilm/whale-instructor\n'
        'Description: Open-source suite for WhalesBot MC101s robot controllers\n'
        ' Blocks/Python/C IDE, Python-to-C transpiler, vendor-free build\n'
        ' pipeline, USB uploader and Bluetooth live run for the WhalesBot\n'
        ' MC101s (E7 Pro / AI S1).\n')

    (root / 'DEBIAN' / 'postinst').write_text(
        '#!/bin/sh\nset -e\n'
        "python3 -c \"import compileall,sys; sys.exit(0 if "
        "compileall.compile_dir('/usr/lib/python3/dist-packages/"
        "whale_instructor', quiet=1) else 1)\" || true\n")
    (root / 'DEBIAN' / 'postinst').chmod(0o755)

    (root / 'usr' / 'share' / 'doc' / 'whale-instructor' / 'copyright'
     ).write_text((HERE.parent / 'LICENSE').read_text())

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    deb = out_dir / f'whale-instructor_{deb_version(version)}_all.deb'
    subprocess.run(['dpkg-deb', '--root-owner-group', '--build',
                    str(root), str(deb)], check=True)
    shutil.rmtree(work, ignore_errors=True)
    return deb


def from_wheel(wheel, out_dir):
    m = re.search(r'whale_instructor-(\d[^-]*)-', wheel.name)
    with tempfile.TemporaryDirectory() as td:
        with zipfile.ZipFile(wheel) as z:
            z.extractall(td)
        return build_deb(Path(td) / 'whale_instructor', m.group(1), out_dir)


def main(argv=None):
    argv = sys.argv if argv is None else argv
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--version', help='package version (default: read from '
                    'pyproject.toml)')
    ap.add_argument('--pkg-tree', default=None,
                    help='path to the whale_instructor package tree '
                    '(default: the one next to this module)')
    ap.add_argument('--wheel', help='build the deb from a built wheel')
    ap.add_argument('-o', '--out', default='dist',
                    help='output directory (default: dist/)')
    args = ap.parse_args(argv[1:])

    if args.wheel:
        return from_wheel(Path(args.wheel), args.out).as_uri() and 0
    version = args.version
    if not version:
        text = (HERE.parent / 'pyproject.toml').read_text()
        version = re.search(r'^version = "(.*?)"', text, re.M).group(1)
    pkg_src = Path(args.pkg_tree or (HERE.parent / 'whale_instructor'))
    print(build_deb(pkg_src, version, args.out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
