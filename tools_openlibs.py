#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compatibility shim — code in whale_instructor/tools_openlibs.py."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from whale_instructor import tools_openlibs  # noqa: E402

if __name__ == '__main__':
    sys.exit(tools_openlibs.main())
