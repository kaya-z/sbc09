# SPDX-License-Identifier: MIT
# SKK dictionary disks for jwp (os9/jwp).
#
# The dictionary is built from an SKK dictionary (SKK-JISYO.L, GPL v2 or later, e.g. the Debian
# package skkdic: apt install skkdic) and put on an OS-9 RBF disk image that jwp reads as /d1:
#     make -C os9/skk                      SKK_utf8.dsk SKK_euc.dsk SKK_sjis.dsk (about 5 MB each)
#     make -C os9/skk floppy               SKK_fd_utf8.dsk ...: a 720K "floppy" with the shortest readings
#     ./sim09 -rom os9/os9v1.rom -0 os9/OS9.dsk -1 os9/SKK_utf8.dsk -v os9/level1
# (use the disk matching the character set of the ROM: os9v1.rom = utf8, os9v1_euc.rom = euc, ...)
SKKJISYO ?= /usr/share/skk/SKK-JISYO.L
MKDIC     = python3 ../../tools/mkskkdic.py
MKRBF     = python3 ../../tools/mkrbf.py

all: SKK_utf8.dsk SKK_euc.dsk SKK_sjis.dsk

floppy: SKK_fd_utf8.dsk SKK_fd_euc.dsk SKK_fd_sjis.dsk

skk_%.dic: $(SKKJISYO) ../../tools/mkskkdic.py
	$(MKDIC) -e $* -o $@ $<

skk_fd_%.dic: $(SKKJISYO) ../../tools/mkskkdic.py
	$(MKDIC) -e $* -b 600 -o $@ $<

SKK_%.dsk: skk_%.dic ../../tools/mkrbf.py
	$(MKRBF) -s 0 -n SKKDIC -o $@ skk.dic=$<

SKK_fd_%.dsk: skk_fd_%.dic ../../tools/mkrbf.py
	$(MKRBF) -s 720 -n SKKDIC -o $@ skk.dic=$<

clean:
	rm -f skk_*.dic SKK_*.dsk

.PHONY: all floppy clean
