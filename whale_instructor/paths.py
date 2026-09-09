#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared path resolution for Whale Instructor.

The package is self-contained: transpiler, builder, uploader, web IDE and
the complete free runtime (open core + upstream ST/FreeRTOS sources) ship
inside it; only the cross toolchain is needed from the outside (see
build_tc.find_gcc / `whale-fetch-toolchain fetch`).
"""
import os
import sys
from pathlib import Path

PACKAGE_DIR = Path(__file__).resolve().parent
REPO_ROOT = PACKAGE_DIR.parent \
    if (PACKAGE_DIR.parent / 'make_publish.py').is_file() else None


def data_dir():
    """Per-OS user data dir for Whale Instructor (~/.local/share and friends).

    Created on demand."""
    if sys.platform == 'darwin':
        base = Path.home() / 'Library' / 'Application Support'
    elif os.name == 'nt':
        base = Path(os.environ.get(
            'LOCALAPPDATA', Path.home() / 'AppData' / 'Local'))
    else:
        base = Path(os.environ.get(
            'XDG_DATA_HOME', Path.home() / '.local' / 'share'))
    d = base / 'whale-instructor'
    d.mkdir(parents=True, exist_ok=True)
    return d
