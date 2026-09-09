#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compatibility shim — the real code lives in whale_instructor/py2c.py."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from whale_instructor import py2c  # noqa: E402

if __name__ == '__main__':
    sys.exit(py2c.main(sys.argv[1:]))
