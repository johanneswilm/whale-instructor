# Contributing

Thanks for your interest! Before any code from outside contributors can be
merged, a Contributor License Agreement (CLA) is required:

- By submitting a contribution (patch, pull request, snippet), you agree
  that Johannes Wilm may relicense your contribution under GPL-3.0-or-later
  or LGPL-3.0-or-later, and under future versions of those licenses.
- Until the formal CLA document is in place, no third-party code is merged.

## Ground rules

- License: GPL-3.0-or-later, with `SPDX-FileCopyrightText: <your name>` and
  `SPDX-License-Identifier: GPL-3.0-or-later` on every new file. Files that
  are linked into user programs (under `whale_instructor/runtime/`) are LGPL-3.0-or-later;
  generated C carries no license markers (see LICENSE_EXCEPTION.md).
- Linux first: tools and the IDE must stay cross-platform in spirit
  (stdlib-only Python, argv-list subprocesses, pathlib, no shell).
- Clean-room rule: never copy or read WhalesBot's proprietary code
  (`pytoc.py`, `pythonbuild.py`, the vendor app) into this project. API
  facts come from headers we compile against and observed behavior.
- No vendor binaries, libraries, headers or firmware images may be
  committed (see PUBLISHING.md for the full exclusion list). Do not put
  the word "whalesbot" in new executable/package/user-facing identifiers.
- Tests: `python3 tests/test_py2c.py` and `node
  tests/ide_generators.test.js` must pass; add tests for new transpiler
  features and new blocks.
