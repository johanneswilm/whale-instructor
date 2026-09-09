#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compatibility shim — code in whale_instructor/tools_fetch_toolchain.py."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from whale_instructor import tools_fetch_toolchain  # noqa: E402

if __name__ == '__main__':
    tools_fetch_toolchain.main()
