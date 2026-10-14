#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""MK09 on the Verilated RTL of the Tang Nano 9K system (fpga/tn9k): the same boot ROM (the FUZIX
boot ROM, which is byte for byte the ROM of MK09), the MC6809 core, the UART, the timer and the SD
card in SPI mode.  Boots the disk image, lists the root directory, makes a directory and a file
(block writes to the card) and powers off.   Run:  tests/fpga_mk09.py     (needs fpga/tn9k/sim/build/fuzix/obj/Vsim_top)"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MK = os.path.dirname(HERE)
ROOT = os.path.dirname(MK)
sys.path.insert(0, HERE)
import fatlib

TB = os.path.join(ROOT, 'fpga', 'tn9k', 'sim', 'build', 'fuzix', 'obj', 'Vsim_top')
IMG = os.path.join(MK, os.environ.get('MK09_BUILD', 'build'), 'mk09.img')
ROMBIN = os.path.join(MK, os.environ.get('MK09_BUILD', 'build'), 'bootrom.bin')
FUZIX_ROM = os.path.join(ROOT, 'fuzix', 'images', 'bootrom.bin')
if not os.path.exists(TB) or not os.path.exists(IMG):
    print('SKIP: the Verilator model (fpga/tn9k/sim) or the MK09 image is not built')
    sys.exit(0)

fails = 0


def check(cond, what):
    global fails
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)


check(open(ROMBIN, 'rb').read() == open(FUZIX_ROM, 'rb').read(),
      'the MK09 boot ROM is the ROM that the FPGA bitstream (ROM=fuzix) has: no new bitstream is needed')
w = tempfile.mkdtemp()
img = os.path.join(w, 'sd.img')
shutil.copy(IMG, img)
script = os.path.join(w, 's.txt')
open(script, 'w').write('expect type help\nsend ls\\r\nexpect ETC\nsend echo hardware >/hw.txt\\r\nexpect /> \n'
                        'send mkdir /hwdir\\r\nexpect /> \nsend cat /hw.txt\\r\nexpect hardware\nsend shutdown\\r\nexpect zzzz\n')
p = subprocess.run([TB, '-d', img, '-s', script, '-c', '4000000000'], capture_output=True, text=True, timeout=1800)
out = p.stdout + p.stderr
check('microkernel' in out and 'BIN/' in out, 'boots from the SD card (SPI) and lists the volume')
check('power off' in out, 'shutdown powers off the board')
v = fatlib.Volume(img)
check(v.read('/HW.TXT') == b'hardware\n' and v.find('/HWDIR') is not None, 'the files written to the card are in the file system')
shutil.rmtree(w, ignore_errors=True)
print('%d failure(s)' % fails)
sys.exit(1 if fails else 0)
