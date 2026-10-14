#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mklink.py - write an lwlink script for an MK09 module.

    mklink.py CODE_BASE [DATA_BASE] > module.link

The code (start, code, rodata, initgl) goes to CODE_BASE; the initialised data and the bss go to
DATA_BASE when given (system modules keep their data above the I/O page), else they follow the code.
The output is meant for  lwlink --format=srec.  Entry point: _start."""
import sys

code = int(sys.argv[1], 16)
data = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
print('define basesympat s_%s')
print('define lensympat l_%s')
print('section start load %x' % code)
for s in ('code', 'rodata', 'initgl_start', 'initgl', 'initgl_end'):
    print('section ' + s)
print('section rwdata' + (' load %x' % data if data is not None else ''))
print('section bss,bss')
print('entry _start')
