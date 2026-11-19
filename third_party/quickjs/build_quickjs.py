# -*- coding: utf-8 -*-
"""Build libquickjs_win64.a with a 64-bit MinGW-w64 gcc.

On Windows, `gcc`/`ar` are expected to be the 64-bit MinGW-w64 tools.  On
Linux, the x86_64-w64-mingw32 cross tools are used.  MINGW_CC and MINGW_AR
may override those defaults.

The distinct win64 name prevents a stale 32-bit archive from being linked
into the 64-bit browser and never collides with Linux libquickjs.a.
"""
import os
import shutil
import subprocess
import sys

IS_WINDOWS = sys.platform.startswith('win')
CC = os.environ.get(
    'MINGW_CC', 'gcc' if IS_WINDOWS else 'x86_64-w64-mingw32-gcc')
AR = os.environ.get(
    'MINGW_AR', 'ar' if IS_WINDOWS else 'x86_64-w64-mingw32-ar')
if shutil.which(CC) is None or shutil.which(AR) is None:
    print('ERROR: 64-bit MinGW-w64 tools were not found:', CC, AR)
    sys.exit(1)

SRC = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get('QJS_WIN64_OBJDIR', os.path.join(SRC, 'obj_win64'))
LIB = os.environ.get('QJS_WIN64_OUT',
                     os.path.join(SRC, 'libquickjs_win64.a'))
os.makedirs(OUT, exist_ok=True)

SRCS = ['quickjs.c', 'quickjs-debugger.c', 'libregexp.c', 'libunicode.c',
        'cutils.c', 'quickjs-libc.c', 'libbf.c']

CFLAGS = [
    '-O2', '-g', '-fwrapv', '-m64',
    '-D_GNU_SOURCE',
    '-DCONFIG_VERSION="2024-01-13"',
    '-DCONFIG_BIGNUM',
    '-D__USE_MINGW_ANSI_STDIO',
    '-DNDEBUG',
    '-Wall', '-Wno-array-bounds', '-Wno-format-truncation',
    '-Wno-unused-parameter', '-Wno-unused-variable',
    '-Wno-unused-but-set-variable',
]

objs = []
for s in SRCS:
    base = os.path.splitext(s)[0]
    o = os.path.join(OUT, base + '.o')
    cmd = [CC] + CFLAGS + ['-c', os.path.join(SRC, s), '-o', o]
    print('CC', s)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print('FAILED:', s)
        print(r.stderr[-4000:])
        sys.exit(1)
    if r.stderr.strip():
        print('  warn:', r.stderr.strip().splitlines()[-1][:200])
    objs.append(o)

r = subprocess.run([AR, 'rcs', LIB] + objs, capture_output=True, text=True)
if r.returncode != 0:
    print('ar FAILED', r.stderr[-2000:])
    sys.exit(1)
print('OK libquickjs_win64.a size:', os.path.getsize(LIB))
