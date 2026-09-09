#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compatibility shim — the real code lives in whale_instructor/whale_cli.py."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from whale_instructor import whale_cli  # noqa: E402

if __name__ == '__main__':
    sys.exit(whale_cli.main())
