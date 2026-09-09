#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a Whale Instructor app image with an arbitrary arm-none-eabi
toolchain, using that toolchain's OWN consistent newlib.

Lesson from the native-link investigation (2026-09-06): NEVER mix crt from one
newlib version with libc from another (struct _reent layout differs). Every
lib/crt/spec must resolve inside ONE toolchain tree; the post-build check
greps the map for foreign paths.

The link is fully vendor-free: runtime/user_api/ supplies the application
framework, API wrappers, patrol engine and newlib stubs; libwhale_open.a +
libwhale_core.a (built by tools_openlibs.py from free upstream sources)
supply the kernel, drivers, startup and device layers.

Usage:
   build_tc.py <user_main.c> <slot> <out.bin> --gcc=<path-to-arm-none-eabi-gcc>
               [--debian]   # dpkg-extracted layout: add -B/-isystem fixes
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from whale_instructor import paths

PACKAGE_DIR = paths.PACKAGE_DIR
LINKERSCRIPTS = {1: 'stm32_flash1.ld', 2: 'stm32_flash2.ld', 3: 'stm32_flash3.ld'}
USER_API = os.path.join(PACKAGE_DIR, 'runtime', 'user_api')
ST_DIR = os.path.join(PACKAGE_DIR, 'runtime', 'open', 'CMSIS',
                      'DeviceSupport', 'ST', 'STM32F10x')
VECT_TAB = {1: '0x19000U', 2: '0x39000U', 3: '0x59000U'}
SRC_RELS = ['app_main.c', 'api_wrappers.c', 'patrol.c', 'syscalls.c']

LEGACY_CFLAGS = ('-Wno-error=implicit-function-declaration '
                 '-Wno-error=implicit-int -Wno-error=int-conversion '
                 '-Wno-error=incompatible-pointer-types '
                 '-Wno-error=return-mismatch').split()


def header_dirs():
    """Include path for user programs and the runtime user layer. Our
    public runtime/ dir comes first so whale_instructor.h wins; the open
    tree supplies the ST/CMSIS/FreeRTOS headers (all free upstream)."""
    return [os.path.join(PACKAGE_DIR, 'runtime'),
            USER_API,
            os.path.join(PACKAGE_DIR, 'runtime', 'open',
                         'STM32F10x_StdPeriph_Driver', 'inc'),
            os.path.join(PACKAGE_DIR, 'runtime', 'open', 'CMSIS'),
            ST_DIR,
            os.path.join(PACKAGE_DIR, 'runtime', 'open', 'FreeRTOS',
                         'include'),
            os.path.join(PACKAGE_DIR, 'runtime', 'open', 'FreeRTOS',
                         'portable', 'GCC', 'ARM_CM3')]


def open_and_core_libs():
    """(open_lib, whale_core_lib) for the vendor-free link, or None.

    tools_openlibs.py builds libwhale_open.a (FreeRTOS + ST StdPeriph +
    CMSIS + the stock ST startup object, all free upstream sources) and
    libwhale_core.a (the Whale Instructor open core: board support,
    queue, exception handlers, debug usart, display, control, audio).
    Both are required: nothing else provides the device API any more.
    """
    open_lib = os.path.join(PACKAGE_DIR, 'runtime', 'open', 'libwhale_open.a')
    whale_core = os.path.join(PACKAGE_DIR, 'runtime', 'open_core',
                              'libwhale_core.a')
    if os.path.isfile(open_lib) and os.path.isfile(whale_core):
        return open_lib, whale_core
    return None


def run(cmd, tag):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        print(r.stdout[-3000:], r.stderr[-3000:], sep='\n')
        sys.exit(f'FAILED ({tag}): ' + ' '.join(cmd[:4]))
    return r


# ---- toolchain discovery (Linux first, macOS/Windows kept open) -----------

def _gcc_exe_name():
    return 'arm-none-eabi-gcc.exe' if os.name == 'nt' else 'arm-none-eabi-gcc'


def candidate_gccs():
    """Yield plausible arm-none-eabi-gcc paths, most preferred first.

    Order: WHALE_INSTRUCTOR_TOOLCHAIN override (old WHALESBOT_TOOLCHAIN
    still honoured; gcc path or toolchain root), the known
    ~/.local/<toolchain>/bin layout (newest first — this is where the
    device-validated xPack toolchains live), toolchains fetched by
    `whale-fetch-toolchain fetch` (<data_dir>/toolchains/*/bin,
    newest first), then PATH (any distro/IDE install; Windows finds
    arm-none-eabi-gcc.exe via PATHEXT).
    """
    exe = _gcc_exe_name()
    env = os.environ.get('WHALE_INSTRUCTOR_TOOLCHAIN') \
        or os.environ.get('WHALESBOT_TOOLCHAIN')
    if env:
        p = Path(env).expanduser()
        yield p / 'bin' / exe if p.is_dir() else p
    local = Path.home() / '.local'
    if local.is_dir():
        for p in sorted(local.glob('*/bin/' + exe), reverse=True):
            yield p
    fetched = paths.data_dir() / 'toolchains'
    if fetched.is_dir():
        for p in sorted(fetched.glob('*/bin/' + exe), reverse=True):
            yield p
    which = shutil.which('arm-none-eabi-gcc')
    if which:
        yield Path(which)


def _usable(p):
    try:
        return p.is_file() and os.access(p, os.X_OK)
    except OSError:
        return False


def find_gcc():
    """First usable arm-none-eabi-gcc, or None. Order as in candidate_gccs:
    env override, ~/.local xPacks, fetched toolchains, PATH."""
    for p in candidate_gccs():
        if _usable(p):
            return str(p)
    return None


def discover_toolchains():
    """All usable arm-none-eabi-gcc's, newest version first (for IDE UI)."""
    found = []
    seen = set()
    for p in candidate_gccs():
        s = str(p)
        if s in seen or not _usable(p):
            continue
        seen.add(s)
        try:
            found.append((gcc_major(s), s))
        except (ValueError, OSError, subprocess.SubprocessError):
            continue
    return [s for _, s in sorted(found, key=lambda t: (-t[0], t[1]))]


def gcc_major(gcc):
    r = subprocess.run([gcc, '-dumpfullversion', '-dumpversion'],
                       capture_output=True, text=True)
    return int((r.stdout or '0').split('.')[0])


def build(user_main, slot, out_bin, gcc, debian, objs_gcc=None, objs_debian=False,
          extra_libs=(), manual_crt=None, swaps=None, float_printf=True,
          function_sections=True):
    """objs_gcc: compile app objects with a DIFFERENT toolchain (object-level
    splitter); libc/crt/libgcc always come from `gcc`'s toolchain at link."""
    work = os.path.join(tempfile.gettempdir(), 'kilo', 'tc_ladder')
    os.makedirs(os.path.join(work, 'objects'), exist_ok=True)
    major = gcc_major(gcc)
    print(f'libs/link toolchain: {gcc} (gcc {major})')
    cc = objs_gcc or gcc
    cc_debian = objs_debian if objs_gcc else debian
    cc_major = gcc_major(cc) if objs_gcc else major
    if objs_gcc:
        print(f'objects toolchain:  {cc} (gcc {cc_major})')

    tc_root = os.path.dirname(os.path.dirname(gcc))  # .../usr or .../bin/..
    cflags = ['-c', '-mcpu=cortex-m3', '-mthumb', '-Wall', '-Os',
              '-mthumb-interwork', '-DUSE_STDPERIPH_DRIVER', '-DSTM32F10X_HD',
              '--specs=nano.specs']
    ldf_pre = []
    cc_pre = []
    if debian:
        nb = os.path.join(tc_root, 'lib', 'arm-none-eabi', 'newlib')
        ldf_pre = ['-B' + nb + '/']
    if cc_debian:
        nb = os.path.join(tc_root, 'lib', 'arm-none-eabi', 'newlib')
        cc_pre = ['-B' + nb + '/', '-isystem',
                  os.path.join(tc_root, 'include', 'newlib')]
    if cc_major >= 14:
        cflags += LEGACY_CFLAGS
    if cc_major >= 15:
        # gcc 15 defaults to -std=gnu23, which rejects the vendor headers'
        # legacy typedefs ("useless type name in empty declaration").
        cflags += ['-std=gnu11']
    if function_sections:
        # let --gc-sections drop unused wrapper/engine functions instead
        # of pulling the whole translation unit into the link
        cflags += ['-ffunction-sections', '-fdata-sections']
    # one -DUSER_TASKN per task entry point found in the generated C
    if user_main.endswith('.py'):
        # transpile the program in place; the IDE does this itself for
        # its build jobs, this is the one-shot CLI convenience path
        from whale_instructor import py2c
        cfile = os.path.join(work, 'objects',
                             os.path.basename(user_main)[:-3]
                             + f'_transpiled_{os.getpid()}.c')
        if py2c.main([user_main, '-o', cfile]) != 0:
            sys.exit('transpile failed: ' + user_main)
        user_main = cfile
    src_text = open(user_main).read()
    for m in sorted(set(re.findall(r'\bvoid (user_task\d+)\s*\(', src_text))):
        cflags.append('-D' + m.upper())

    incs = ['-I' + d for d in header_dirs()]
    srcs = [os.path.join(USER_API, SRC_RELS[0]), os.path.abspath(user_main)]
    srcs += [os.path.join(USER_API, s) for s in SRC_RELS[1:]]
    srcs.append(os.path.join(PACKAGE_DIR, 'runtime', 'assert_override.c'))
    srcs.append(os.path.join(ST_DIR, 'system_stm32f10x.c'))
    objs = []
    for src in srcs:
        obj = os.path.join(work, 'objects',
                           f'{os.path.basename(src)}_{os.getpid()}.o')
        extra = (['-DVECT_TAB_OFFSET=' + VECT_TAB[slot]]
                 if src.endswith('system_stm32f10x.c') else [])
        run([cc] + cc_pre + cflags + incs + extra + [src, '-o', obj],
            'compile')
        objs.append(obj)

    script = os.path.join(USER_API, 'linker', LINKERSCRIPTS[slot])
    elf = os.path.join(work, f'APP_{os.path.basename(out_bin)}.elf')
    mapf = elf + '.map'
    if manual_crt:
        # Object-level bisect link: every crt/lib file explicit, so crt+libc
        # can come from toolchain A while libgcc comes from toolchain B.
        # (Plain builds use the vendor-replica driver link; this path only
        # exists for library bisects.)
        mult = subprocess.run(
            [manual_crt, '-mcpu=cortex-m3', '-mthumb',
             '-print-multi-directory'], capture_output=True, text=True
        ).stdout.strip()
        tc_root = os.path.dirname(os.path.dirname(manual_crt))
        crtdir = os.path.join(tc_root, 'arm-none-eabi', 'lib', mult)
        gccdir = os.path.join(tc_root, 'lib', 'gcc', 'arm-none-eabi')
        gccver = os.listdir(gccdir)[0]
        gccdir_ml = os.path.join(gccdir, gccver, mult)
        crt0_o = os.path.join(crtdir, 'rdimon-crt0.o')
        libc_a = os.path.join(crtdir, 'libc_nano.a')
        rdimon_a = os.path.join(crtdir, 'librdimon_nano.a')
        libm_a = os.path.join(crtdir, 'libm.a')
        for name, path in (swaps or {}).items():
            if name == 'crt0':
                crt0_o = path
            elif name == 'libc':
                libc_a = path
            elif name == 'rdimon':
                rdimon_a = path
            elif name == 'libm':
                libm_a = path
            else:
                sys.exit('unknown --swap component: ' + name)
        crt_objs = [os.path.join(gccdir_ml, 'crti.o'),
                    os.path.join(gccdir_ml, 'crtbegin.o'),
                    crt0_o]
        tail_objs = [os.path.join(gccdir_ml, 'crtend.o'),
                     os.path.join(gccdir_ml, 'crtn.o')]
        libs = [libm_a, libc_a, rdimon_a]
        ldf = ['-nostdlib', '-T', script, '-mcpu=cortex-m3', '-mthumb',
               '-mthumb-interwork', '-Wl,-Map=' + mapf, '-o', elf,
               '-Xlinker', '--gc-sections', '-u', '_printf_float']
        oc = open_and_core_libs()
        if oc:
            # group: core.a members reference data in open.a and vice versa
            link_libs = (['-Wl,--start-group'] + list(oc) + libs
                         + ['-Wl,--end-group'])
        else:
            sys.exit('libwhale_open.a / libwhale_core.a missing — run:\n'
                     '    python3 -m whale_instructor.tools_openlibs')
        run([gcc] + ldf + crt_objs + objs + extra_libs + link_libs
            + tail_objs, 'link')
    else:
        ldf = ldf_pre + ['-T', script, '-mcpu=cortex-m3', '-mthumb',
                         '-mthumb-interwork', '-Wl,-Map=' + mapf, '-o', elf,
                         '--specs=nano.specs', '-Xlinker', '--gc-sections',
                         '--specs=rdimon.specs']
        if float_printf:
            ldf += ['-u', '_printf_float']
        oc = open_and_core_libs()
        if oc:
            link_libs = (['-Wl,--start-group'] + list(oc) + ['-lm']
                         + ['-Wl,--end-group'])
        else:
            sys.exit('libwhale_open.a / libwhale_core.a missing — run:\n'
                     '    python3 -m whale_instructor.tools_openlibs')
        run([gcc] + ldf + objs + extra_libs + link_libs, 'link')
    objcopy = os.path.join(os.path.dirname(gcc), 'arm-none-eabi-objcopy')
    if not os.path.isfile(objcopy):
        objcopy = gcc.replace('-gcc', '-objcopy')
    run([objcopy, '-O', 'binary', '-S', elf, out_bin], 'objcopy')

    # PROVENANCE CHECK: every lib/crt in the map must live in THIS toolchain
    # tree or the runtime tree — mixing newlib versions = crashes.
    foreign = []
    with open(mapf, errors='replace') as f:
        for line in f:
            for m in re.finditer(r'(/\S+?\.a)\(', line):
                p = m.group(1)
                if p.startswith(str(PACKAGE_DIR)):
                    continue
                if tc_root.rstrip('/') not in p and '/arm-none-eabi/' not in p:
                    foreign.append(p)
    foreign = sorted(set(foreign))
    if foreign:
        print('WARNING foreign libs in map:')
        for p in foreign:
            print('  ', p)
    else:
        print('provenance OK: all archive libs from one toolchain tree')
    print(f'built {out_bin}: {os.path.getsize(out_bin)} bytes')
    return mapf


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    args = [a for a in argv if not a.startswith('--')]
    opts = {}
    for a in argv:
        if a.startswith('--') and '=' in a:
            k, v = a[2:].split('=', 1)
            opts[k] = v
    if len(args) < 3:
        print(__doc__)
        return 1
    gcc = opts.get('gcc') or find_gcc()
    if gcc is None:
        sys.exit('no arm-none-eabi-gcc found. Either install '
                 'gcc-arm-none-eabi via your system package manager '
                 '(e.g. "sudo apt install gcc-arm-none-eabi", '
                 '"brew install arm-none-eabi-gcc"),\nor fetch a prebuilt '
                 'xPack toolchain into the user data dir:\n'
                 '    whale-fetch-toolchain fetch\n'
                 '(or pass --gcc=<path> / set WHALE_INSTRUCTOR_TOOLCHAIN)')
    mapf = build(args[0], int(args[1]), args[2], gcc,
                 '--debian' in argv,
                 objs_gcc=opts.get('objs-gcc'),
                 objs_debian='--objs-debian' in argv,
                 extra_libs=[v for k, v in
                             (a[2:].split('=', 1) for a in argv
                              if a.startswith('--extra='))],
                 manual_crt=opts.get('manual-crt'),
                 swaps={a[7:].split('=', 1)[0]: a[7:].split('=', 1)[1]
                        for a in argv if a.startswith('--swap=')},
                 float_printf='--no-float-printf' not in argv,
                 function_sections='--no-function-sections' not in argv)
    print('map:', mapf)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
