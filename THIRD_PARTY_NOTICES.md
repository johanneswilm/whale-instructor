# Third-party components

This project bundles the following third-party libraries in
`whale_instructor/static/vendor/`. They are used unmodified and keep their own licenses.

## Blockly 9.3.3

- Files: `blockly_compressed.js`, `blocks_compressed.js`, `msg_en.js`
- License: Apache License 2.0
- Copyright: Google LLC / the Blockly authors
- Source: https://github.com/google/blockly

## CodeMirror 5.65.16

- Files: `codemirror.min.js`, `codemirror.min.css`,
  `material-darker.min.css`, `python.min.js`, `clike.min.js`
- License: MIT
- Copyright: Marijn Haverbeke and contributors
- Source: https://codemirror.net/5

## Note

All other project-authored code is GPL-3.0-or-later (see LICENSE); the
runtime pieces linked into user programs (`whale_instructor/runtime/whale_instructor.h`,
`whale_instructor/runtime/assert_override.c`) are LGPL-3.0-or-later, and user programs keep
their own license — see LICENSE_EXCEPTION.md. `whale_instructor/runtime/open/` holds free
upstream sources (FreeRTOS, ST StdPeriph/USB, CMSIS) under their own
licenses — see `whale_instructor/runtime/open/README.md`. No WhalesBot-proprietary code or
binaries are included or distributed; see PUBLISHING.md for the exclusion
rules.
