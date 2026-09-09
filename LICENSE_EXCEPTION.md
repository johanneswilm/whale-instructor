# Whale Instructor licensing: what applies to what

Whale Instructor is copyleft software: the IDE, the transpiler, the build
tools and the uploader are **GPL-3.0-or-later** (see `LICENSE`). But
**programs that YOU write with Whale Instructor are yours** — we do not
want (and legally do not need) to force your robot programs to become GPL.

## 1. Your programs stay yours

Whale Instructor is a tool. Like a compiler, it does not take ownership of,
or impose a license on, its output. To make that explicit and binding, the
following **additional permission** under GNU GPL version 3 section 7
applies to all of Whale Instructor:

> **Additional permission under GNU GPL version 3 section 7**
>
> The copyright holders of Whale Instructor grant you additional permission
> to use, copy, modify and convey programs that you write with the help of
> Whale Instructor — including Python programs written in the `whale`
> dialect, block programs, the C code generated from them by py2c.py, and
> firmware images built from them — under terms of your own choosing,
> without any obligation to license them under the GNU GPL or to disclose
> their source code.
>
> If you modify Whale Instructor itself, or any covered work, by linking or
> combining it with the Whale Instructor runtime (or a modified version of
> that runtime), or with vendor firmware libraries supplied under
> `runtime_private/`, containing parts covered by the terms of an
> independent license, the licensors of this Program also grant you
> permission to convey the resulting work.

(The first paragraph is the permission that matters for users; the second
follows the well-established "GCC Runtime Library exception" pattern and
makes building against the runtime and the vendor libraries unambiguous.)

## 2. The runtime is LGPL-3.0-or-later

Files that end up *inside* your program image — currently
`runtime/whale_instructor.h` and `runtime/assert_override.c` — are licensed
**LGPL-3.0-or-later** rather than GPL, like a classic runtime library. You
may write closed-source programs that use them, and you may modify them as
long as you respect the LGPL (share runtime changes you distribute).

## 3. The vendor libraries are not ours and are not distributed

`libMercuryController.a` (or its stripped-down form `mercury_core.a`), the
vendor's `whalesbot.h`/`whalesbot.c`, linker scripts and clock configs come
from WhalesBot's own software. They carry **no license** (all rights
reserved). You extract them from your own copy of the vendor application
into `runtime_private/`; they never leave your machine and are never
redistributed by this project. The freely licensed parts of that archive
(FreeRTOS V9.0.0, ST StdPeriph, CMSIS, ST USB core) are rebuilt from open
source in `runtime/open/` so the user-supplied blob shrinks to the
genuinely proprietary vendor core — see `runtime/open/README.md`.

## 4. Third-party front-end libraries

Blockly and CodeMirror are vendored under their own permissive licenses —
see `THIRD_PARTY_NOTICES.md`.
