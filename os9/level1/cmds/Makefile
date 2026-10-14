# SPDX-License-Identifier: MIT
# OS-9 Level 1 commands, assembled from the NitrOS-9 sources with a09
SRC1  = ../../nitros9-code/level1/cmds
A09   = ../../../tools/a09 -I ../../nitros9-code/defs/

CMDS = asm attr calldbg cmp config copy cputype date debug del deldir devs dir_cb dirsort dump \
       echo edit disasm free ident irqs link list load makdir mdir mfree \
       procs prompt shellplus sleep tee touch tsmon unlink

all : dhry jwp help error $(CMDS) dir kmode nc login shutdown pxd pwd

$(CMDS) :
	$(A09) -l $@.lst -o $@ $(SRC1)/$@.asm

pxd :
	$(A09) -D PXD -l $@.lst -o $@ $(SRC1)/pd.asm
pwd :
	$(A09) -D PWD -l $@.lst -o $@ $(SRC1)/pd.asm

dir : ../../kanji/dir.asm
	$(A09) -l $@.lst -o $@ ../../kanji/dir.asm
nc : ../../net/nc.asm
	$(A09) -l $@.lst -o $@ ../../net/nc.asm
login : ../../net/login.asm
	$(A09) -l $@.lst -o $@ ../../net/login.asm
shutdown : ../../misc/shutdown.asm
	$(A09) -l $@.lst -o $@ ../../misc/shutdown.asm
# Dhrystone (os9/dhry) and the word processor (os9/jwp) are built with CMOC when it is installed; prebuilt modules are part of the distribution
dhry :
	$(MAKE) -C ../../dhry dhry
	cp ../../dhry/dhry dhry
.PHONY : dhry
# the word processor is built (CMOC, if installed) in os9/jwp; a prebuilt module is part of the distribution
jwp :
	$(MAKE) -C ../../jwp jwp
	cp ../../jwp/jwp jwp
.PHONY : jwp
kmode : ../../kanji/kmode.asm
	$(A09) -l $@.lst -o $@ ../../kanji/kmode.asm

# help and error have their data area (org 0) behind the module name, which a09 turns into a
# wrong execution offset / module size: fix_org.py reorders the source
help error :
	python3 ../../../tools/fix_org.py $(SRC1)/$@.asm $@.fixed.asm
	$(A09) -I $(SRC1)/ -l $@.lst -o $@ $@.fixed.asm

clean :
	rm -f dhry jwp shutdown *.lst *.fixed.asm help error dir kmode nc login $(CMDS)
