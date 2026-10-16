# SPDX-License-Identifier: MIT
# OS-9 (NitrOS-9) Level 1 / Level 2 ROM images for the simulator.
# The modules are assembled from the NitrOS-9 sources (nitros9-code/) and the
# few sbc09 specific modules in level1/ and level2/ with the a09 assembler
# (tools/a09, from the sbc09 project).
MAKE9ROM = ../tools/make9rom

all: os9v1.rom os9v1_tn9k.rom os9v1boot.rom OS9Boot.bin bootdisk os9v2.rom os9v2_tn9k.rom os9v1_euc.rom os9v2_euc.rom os9v1_sjis.rom os9v2_sjis.rom cmds

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

# the same for the FPGA board (the boot message names the machine: Tang Nano 9K)
os9v1_tn9k.rom: CS =
os9v1_tn9k.rom: modules
	$(MAKE9ROM) -o $@ $(subst level1/os9p1,level1/os9p1_tn9k,$(subst level1/init,level1/init_tn9k,$(L1MODS)))

# Level 1 booting from the disk: the ROM (8 KB at $E000) holds the kernel, Init, the booter, IOMan and SCF (they take
# 15 pages of RAM less than in the boot file: jwp needs the room); everything else is the boot
# file OS9Boot (OS9Boot.bin: the modules one after the other, written to a disk image by tools/mkboot.py)
BOOTMODS = level1/rbf level1/pdisk level1/d0 level1/d1 level1/vrbf level1/v0 level1/clock \
           level1/term$(CS) level1/pty level1/npty level1/n1$(CS) level1/n2$(CS) level1/n3$(CS) level1/shell level1/sysgo
os9v1boot.rom: modules
	$(MAKE9ROM) -o $@ level1/boot level1/init_tn9k level1/ioman level1/scf level1/os9p2 level1/os9p1_tn9k

OS9Boot.bin: modules
	cat $(BOOTMODS) > $@

# put it on OS9.dsk (the disk also boots the machine of os9v1boot.rom; the ROM images ignore it)
# and the kernel itself (Boot, Init, IOMan, SCF, KrnP2, Krn) as the image the 256 byte boot ROM of FUZIX / MK09 loads from
# LSN 2 (tools/mkkernel.py): the machine then boots OS-9 from the card with the same FPGA bitstream as FUZIX and MK09
# (the rest of the image goes on at LSN 720, which makes the disk a little larger)
KERNMODS = level1/boot level1/init_tn9k level1/ioman level1/scf level1/os9p2 level1/os9p1_tn9k
# the disk of the Level 2 machine of the FPGA board (os9l2): the Level 2 commands, 1440 sectors (tools/mkl2disk.py needs sim09)
OS9L2.dsk: OS9.dsk os9v2.rom
	python3 ../tools/mkl2disk.py -o $@

bootdisk: OS9Boot.bin
	python3 ../tools/mkkernel.py -e 720 OS9.dsk $(KERNMODS)
	python3 ../tools/mkboot.py OS9.dsk OS9Boot.bin

os9v2.rom os9v2_euc.rom os9v2_sjis.rom: modules
	$(MAKE9ROM) -o $@ -2 $(L2MODS)

# Level 2 for the FPGA board (the ROM image is copied from the SPI flash to the RAM at the start: docs/FPGA.md): the machine is named
# Tang Nano 9K, the disk is /d0 (no /v0)
os9v2_tn9k.rom: CS =
os9v2_tn9k.rom: modules
	$(MAKE9ROM) -o $@ -2 $(subst level2/sysgo,level2/sysgo_tn9k,$(subst level2/init,level2/init_tn9k,$(L2MODS)))

clean:
	$(MAKE) -C level1 clean
	$(MAKE) -C level2 clean
	$(MAKE) -C level1/cmds clean
	$(MAKE) -C level2/cmds clean
	$(MAKE) -C jwp clean
	$(MAKE) -C dhry clean
	rm -f os9v*.rom OS9Boot.bin

.PHONY: all modules cmds bootdisk clean
