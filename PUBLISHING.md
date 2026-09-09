# Publishing checklist

`make_publish.py` builds a clean `publish/` tree from a strict whitelist;
everything not whitelisted stays private. The archive tree is gitignored
and never published.

## Never publish (hard rules)

| Material | Why |
|---|---|
| archive binaries of any kind (`*.a`, `*.dll`, `*.exe`, `*.deb`) | binaries, vendor- or RE-derived (`libwhale_open.a`/`libwhale_core.a` are rebuilt by the user via `tools_openlibs.py`, not shipped) |
| `*.bin` app images (`APP_N.bin`, …) | device images are the user's build output |
| `captures/`, `*.log`, `symbols.txt`, strace dumps | research artifacts, machine-internal data |
| `artifacts/` (build outputs incl. IDE builds) | derived images |

## Before publishing

1. Commit only whitelisted files (the `.gitignore` blocks the rest — verify
   with `git status` that nothing vendor-derived is staged).
2. Run `python3 make_publish.py` and make sure the checks pass. It refuses
   the tree when it finds binaries, images, private paths or missing SPDX
   headers in our source files.
3. Review `git log` of the publish branch: every commit must contain only
   whitelisted paths.
4. Re-read the diff of anything under `whale_instructor/static/vendor/` when
   upgrading Blockly/CodeMirror and update `THIRD_PARTY_NOTICES.md` versions.
5. CONTRIBUTING.md must be present: outside contributions require a signed
   CLA (relicensing rights to Johannes Wilm) before any third-party code is
   merged.
6. Push only the `publish/` tree content to the public repository, e.g.:
   `git -C publish init && git -C publish remote add origin <url>` — or keep
   a dedicated public branch with the same content.
7. For a PyPI release, build from the publish tree (it carries
   `pyproject.toml` + `MANIFEST.in`) with `python3 -m build` and upload the
   wheel/sdist; the wheel must never contain `.a`/`.bin`/`.exe` files (the
   MANIFEST excludes them) — spot-check with `unzip -l`.

## License notes

- Project-authored code: GPL-3.0-or-later, `SPDX-FileCopyrightText:
  Johannes Wilm` on every authored file.
- Runtime pieces linked INTO user programs
  (`whale_instructor/runtime/whale_instructor.h`,
  `whale_instructor/runtime/assert_override.c`):
  LGPL-3.0-or-later — user programs stay the user's property, see
  LICENSE_EXCEPTION.md.
- Generated C (py2c.py output) carries NO SPDX/GPL markers: it belongs to
  the program's author.
- `whale_instructor/runtime/open/` holds free upstream sources (FreeRTOS
  V9.0.0 GPL-2.0 +
  FreeRTOS exception, ST StdPeriph + USB core under ST's permissive
  license, ARM CMSIS under its License.doc) — provenance in
  `runtime/open/README.md`.
- Vendored browser libraries keep their own licenses: Blockly (Apache-2.0),
  CodeMirror (MIT) — documented in THIRD_PARTY_NOTICES.md.
- Protocol facts learned by reverse engineering are used; no vendor code is
  copied (clean-room codegen: py2c.py was written from whalesbot.h facts and
  observed runtime behavior, never from the vendor's generators).
