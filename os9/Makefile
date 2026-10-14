# SPDX-License-Identifier: MIT
# OS-9 (NitrOS-9) Level 1 / Level 2 ROM images for the simulator.
# The modules are assembled from the NitrOS-9 sources (nitros9-code/) and the
# few sbc09 specific modules in level1/ and level2/ with the a09 assembler
# (tools/a09, from the sbc09 project).
MAKE9ROM = ../tools/make9rom

all: os9v1.rom os9v1boot.rom OS9Boot.bin bootdisk os9v2.rom os9v1_euc.rom os9v2_euc.rom os9v1_sjis.rom os9v2_sjis.rom cmds

modules:
	$(MAKE) -C level1
	$(MAKE) -C level2

cmds:
	$(MAKE) -C dhry
	$(MAKE) -C jwp
	$(MAKE) -C level1/cmds
	$(MAKE) -C level2/cmds

# module lists; CS selects the terminal character set variant of term and the N descriptors
L1MODS = level1/shell level1/sysgo level1/ioman level1/term$(CS) level1/pty level1/pdisk level1/d0 \
         level1/d1 level1/vrbf level1/v0 level1/clock level1/scf level1/rbf level1/init \
         level1/npty level1/n1$(CS) level1/n2$(CS) level1/n3$(CS) \
         level1/os9p2 level1/os9p1
L2MODS = level2/shell level2/dir level2/d1 level2/ioman level2/os9p3_perr level2/os9p4_regdump \
         level2/pipe level2/piper level2/pipeman level2/v0 level2/vrbf level2/scf level2/rbf \
         level2/os9p2 level2/sysgo level2/clock level2/pdisk level2/d0 level2/pty level2/term$(CS) \
         level2/init level2/boot level2/npty level2/n1$(CS) level2/n2$(CS) level2/n3$(CS) \
         level2/os9p1

os9v1.rom: CS =
os9v1_euc.rom: CS = _euc
os9v1_sjis.rom: CS = _sjis
os9v2.rom: CS =
os9v2_euc.rom: CS = _euc
os9v2_sjis.rom: CS = _sjis

os9v1.rom os9v1_euc.rom os9v1_sjis.rom: modules
	$(MAKE9ROM) -o $@ $(L1MODS)

# Level 1 booting from the disk: the ROM holds only the kernel, Init and the booter; everything else is the boot
# file OS9Boot (OS9Boot.bin: the modules one after the other, written to a disk image by tools/mkboot.py)
BOOTMODS = level1/ioman level1/rbf level1/scf level1/pdisk level1/d0 level1/d1 level1/vrbf level1/v0 level1/clock \
           level1/term$(CS) level1/pty level1/npty level1/n1$(CS) level1/n2$(CS) level1/n3$(CS) level1/shell level1/sysgo
os9v1boot.rom: modules
	$(MAKE9ROM) -o $@ level1/boot level1/init level1/os9p2 level1/os9p1

OS9Boot.bin: modules
	cat $(BOOTMODS) > $@

# put it on OS9.dsk (the disk also boots the machine of os9v1boot.rom; the ROM images ignore it)
bootdisk: OS9Boot.bin
	python3 ../tools/mkboot.py OS9.dsk OS9Boot.bin

os9v2.rom os9v2_euc.rom os9v2_sjis.rom: modules
	$(MAKE9ROM) -o $@ -2 $(L2MODS)

clean:
	$(MAKE) -C level1 clean
	$(MAKE) -C level2 clean
	$(MAKE) -C level1/cmds clean
	$(MAKE) -C level2/cmds clean
	$(MAKE) -C jwp clean
	$(MAKE) -C dhry clean
	rm -f os9v*.rom OS9Boot.bin

.PHONY: all modules cmds bootdisk clean
