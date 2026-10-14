# SPDX-License-Identifier: MIT
# MK09 - a microkernel for the MC6809 (sim09 / Tang Nano 9K).  Build:  make   (needs CMOC and lwtools:
# tools/install_cmoc.sh PREFIX in the parent directory installs both;  make CMOC_BIN=PREFIX/bin)
#
#   make            kernel, servers, programs, boot ROM, disk image (build/)
#   make test       run the tests (needs ../sim09)
#   make run        boot the disk image in the simulator

CMOC_BIN ?=
CMOC   := $(if $(CMOC_BIN),$(CMOC_BIN)/,)cmoc
LWASM  := $(if $(CMOC_BIN),$(CMOC_BIN)/,)lwasm
LWLINK := $(if $(CMOC_BIN),$(CMOC_BIN)/,)lwlink
LWAR   := $(if $(CMOC_BIN),$(CMOC_BIN)/,)lwar
PYTHON ?= python3 -X utf8
B      ?= build
SIM    ?= ../sim09

CMOCLIB := $(shell dirname "$$(command -v $(CMOC) 2>/dev/null)" 2>/dev/null)/../share/cmoc/lib
INCS    := -Iinclude -Iarch/m6809 -Ibsp/sbc09 -I$(B)
CFLAGS  := --lwasm=$(LWASM) --void-target --no-relocate -O2 -fomit-frame-pointer -Dmain=mk_main $(INCS)
ASFLAGS := -fobj -I$(B) -Iinclude

vpath %.c kernel arch/m6809 bsp/sbc09 servers lib apps apps/jwp
vpath %.s arch/m6809 lib

# the addresses of the memory map (computed from the slot sizes in memmap.mk)
-include $(B)/memmap.gen.mk
HDRS := $(wildcard include/*.h arch/m6809/*.h bsp/sbc09/*.h) $(B)/memmap.h $(B)/memmap.inc $(B)/sysnum.inc

all: $(B)/boot.img $(B)/bootrom.bin $(B)/mk09.img

$(B):
	mkdir -p $(B)

$(B)/memmap.h $(B)/memmap.inc $(B)/memmap.gen.mk: memmap.mk tools/genmap.py | $(B)
	$(PYTHON) tools/genmap.py memmap.mk $(B) $(MAPOVER)

$(B)/sysnum.inc: include/sysnum.h | $(B)
	sed -n 's/^\#define \(SYS_[A-Z]*\)[ \t]*\(.*\)/\1\tequ\t\2/p' $< > $@

$(B)/%.o: %.c $(HDRS)
	$(CMOC) -c $(CFLAGS) -o $@ $<

$(B)/%.o: %.s $(HDRS)
	$(LWASM) $(ASFLAGS) -o $@ $<

$(B)/bootrom.bin: boot/bootrom.s | $(B)
	$(LWASM) --format=raw -o $@ $<

# ---- the library for the servers and programs ----
SYSSTUBS := mk_send mk_recv mk_call mk_reply mk_notify mk_tinfo mk_tkill mk_callg mk_copyfrom mk_copyto mk_tload \
	mk_sleep mk_ticks mk_yield mk_exit mk_self mk_hook mk_tstart mk_tcreate mk_treap mk_power
MEMFUNCS := memcpy memmove memset
LIBMK_OBJS := $(patsubst %,$(B)/sys_%.o,$(SYSSTUBS)) $(patsubst %,$(B)/mem_%.o,$(MEMFUNCS)) \
	$(patsubst lib/%.c,$(B)/%.o,$(wildcard lib/mk_*.c))

$(B)/sys_%.o: lib/mksys.s $(HDRS)
	$(LWASM) $(ASFLAGS) -DSTUB_$*=1 -o $@ $<
$(B)/mem_%.o: lib/mem.s $(HDRS)
	$(LWASM) $(ASFLAGS) -DFUNC_$*=1 -o $@ $<
$(B)/libmk.a: $(LIBMK_OBJS)
	rm -f $@; $(LWAR) -c $@ $^

# ---- system modules: name, objects, code slot, data slot ----
# KERNEL=asm (default): the kernel in 6809 assembler; KERNEL=c: the portable C kernel
KERNEL ?= asm
ifneq ($(KERNEL),asm)
MAPOVER := K_CSZ=0F40 K_DSZ=0280
endif
ifeq ($(KERNEL),asm)
KERNEL_OBJS := $(B)/kernel_asm.o
$(B)/kernel_asm.o: arch/m6809/kernel.s $(HDRS)
	$(LWASM) $(ASFLAGS) -o $@ $<
else
KERNEL_OBJS := $(B)/entry.o $(B)/arch.o $(B)/kernel.o $(B)/bsp.o
endif
TTY_OBJS    := $(B)/crt0.o $(B)/tty.o
DISK_OBJS   := $(B)/crt0.o $(B)/disk.o
FS_OBJS     := $(B)/crt0.o $(B)/fs.o $(B)/fs_core.o $(B)/rtc.o
PM_OBJS     := $(B)/crt0.o $(B)/pm.o $(B)/rtc.o

LINK = $(LWLINK) --format=srec --script=$(B)/$(1).link --map=$(B)/$(1).map -o $$@ $$($(2)_OBJS) -L$(B) -lmk -L$(CMOCLIB) -lcmoc-crt-void -lcmoc-std-void

define SYSMOD
$(B)/$(1).link: memmap.mk tools/mklink.py | $(B)
	$(PYTHON) tools/mklink.py $$($(2)_CODE) $$($(2)_DATA) > $$@
$(B)/$(1).s19: $$($(2)_OBJS) $(B)/libmk.a $(B)/$(1).link tools/chkmap.py
	$(call LINK,$(1),$(2))
	$(PYTHON) tools/chkmap.py $(1) $(B)/$(1).map $$($(2)_CODE) $$($(2)_CSZ) $$($(2)_DATA) $$($(2)_DSZ) $(3)
endef

# kernel code slot symbol names: K_ -> KERNEL
KERNEL_CODE := $(K_CODE)
KERNEL_CSZ  := $(K_CSZ)
KERNEL_DATA := $(K_DATA)
KERNEL_DSZ  := $(K_DSZ)
$(eval $(call SYSMOD,kernel,KERNEL,0))
$(eval $(call SYSMOD,tty,TTY,B4))
$(eval $(call SYSMOD,disk,DISK,82))
$(eval $(call SYSMOD,fs,FS,140))
$(eval $(call SYSMOD,pm,PM,C8))

SYSMODS := kernel tty disk fs pm

# ---- programs (MX executables): apps/NAME.c -> build/NAME.mx ----
APPS      := sh ls cat echo stty cp mv rm mkdir date df ps hd wc spin stk fstest faults ipcbench dhry jwp
BASE_sh   := $(SH_BASE)
STACK_sh  := 640
STACK_jwp := 1200

# jwp: the character set of the console and of the dictionary (utf8, euc or sjis) and the size
# of the text buffer
CHARSET   ?= utf8
JWP_BUF   ?= 11500
JWP_OPT   ?= -fomit-frame-pointer
APPBASE    = $(or $(BASE_$(1)),$(TPA_BASE))
APPSTACK   = $(or $(STACK_$(1)),512)
APPLIBS   := -L$(B) -lmk -L$(CMOCLIB) -lcmoc-crt-void -lcmoc-std-void

define APP
$(B)/crt0app_$(1).o: crt0app.s $(HDRS)
	$(LWASM) $(ASFLAGS) -DSTACKSZ=$(call APPSTACK,$(1)) -DMXFLAGS=$(or $(FLAGS_$(1)),0) -o $$@ $$<
$(B)/$(1).link: memmap.mk tools/mklink.py | $(B)
	$(PYTHON) tools/mklink.py $(call APPBASE,$(1)) > $$@
$(B)/$(1).mx: $(B)/crt0app_$(1).o $(B)/$(1).o $(B)/libmk.a $(B)/$(1).link
	$(LWLINK) --format=raw --script=$(B)/$(1).link --map=$(B)/$(1).map -o $$@ $(B)/crt0app_$(1).o $(B)/$(1).o $(APPLIBS)
endef
$(foreach a,$(APPS),$(eval $(call APP,$(a))))

$(B)/msgs_$(CHARSET).h: apps/jwp/genmsgs.py | $(B)
	$(PYTHON) apps/jwp/genmsgs.py $(CHARSET) > $@
$(B)/msgs.h: $(B)/msgs_$(CHARSET).h
	cp $< $@
$(B)/jwp.o: jwp.c skk.c $(B)/msgs.h $(HDRS)
	$(CMOC) -c --lwasm=$(LWASM) --void-target -O2 $(JWP_OPT) -DBUFSZ=$(JWP_BUF) -Dmain=mk_main $(INCS) -Iapps/jwp -o $@ $<

# ---- the file tree of the disk ----
# SKK dictionary for jwp (tools/mkskkdic.py, from the Debian package skkdic: SKK-JISYO.L); SKK_KB
# limits its size (0 = everything, about 4.5MB)
ICONV_utf8 = UTF-8
ICONV_euc = EUC-JP
ICONV_sjis = CP932
SKKJISYO ?= /usr/share/skk/SKK-JISYO.L
SKK_KB   ?= 0
HAVE_SKK := $(wildcard $(SKKJISYO))
$(B)/skk_$(CHARSET).dic: ../tools/mkskkdic.py $(SKKJISYO)
	$(PYTHON) ../tools/mkskkdic.py -e $(CHARSET) -b $(SKK_KB) -o $@ $(SKKJISYO)

TREE := $(B)/tree
$(B)/tree.stamp: $(foreach a,$(APPS),$(B)/$(a).mx) $(wildcard disk/etc/* disk/doc/*) $(if $(HAVE_SKK),$(B)/skk_$(CHARSET).dic) Makefile
	rm -rf $(TREE); mkdir -p $(TREE)/bin $(TREE)/etc $(TREE)/doc $(TREE)/skk
	for a in $(APPS); do cp $(B)/$$a.mx $(TREE)/bin/$$a.mx; done
	for d in etc doc; do if [ -d disk/$$d ] && [ -n "$$(ls disk/$$d)" ]; then cp disk/$$d/* $(TREE)/$$d/; fi; done
	printf '# /ETC/RC.SH - run by the shell when the system starts\nstty $(CHARSET)\n' > $(TREE)/etc/rc.sh
	if [ -n "$(HAVE_SKK)" ]; then cp $(B)/skk_$(CHARSET).dic $(TREE)/skk/skk.dic; fi
	iconv -f UTF-8 -t $(ICONV_$(CHARSET)) disk/doc/guide.txt > $(TREE)/doc/guide.txt
	touch $@

$(B)/mk09.img: $(B)/boot.img $(B)/tree.stamp tools/mkdisk.py
	$(PYTHON) tools/mkdisk.py -o $@ --boot $(B)/boot.img --size-mb $(DISK_MB) $(TREE)

DISK_MB ?= 16

$(B)/boot.img: $(foreach m,$(SYSMODS),$(B)/$(m).s19) tools/mkimage.py
	$(PYTHON) tools/mkimage.py -o $@ -e 0x$(K_CODE) $(foreach m,$(SYSMODS),$(B)/$(m).s19)

run: all
	$(SIM) -rom $(B)/bootrom.bin -0 $(B)/mk09.img

# the same system for a console in EUC-JP and in Shift_JIS (jwp and its dictionary depend on the character set)
charsets:
	$(MAKE) B=build_euc CHARSET=euc
	$(MAKE) B=build_sjis CHARSET=sjis

# the host test of the file system code (random operations against a model; the volume is checked with fsck.fat)
$(B)/fs_host: tests/fs_host.c servers/fs_core.c servers/fs_core.h
	gcc -DMK_HOST -O1 -Wall -Wno-unused -Iinclude -Iservers -o $@ tests/fs_host.c servers/fs_core.c

fstest: $(B)/fs_host $(B)/mk09.img
	for seed in 1 2 3; do cp $(B)/mk09.img $(B)/fs_t.img; $(B)/fs_host $(B)/fs_t.img random $$seed 500 || exit 1; \
	  dd if=$(B)/fs_t.img of=$(B)/fs_v.img bs=512 skip=2048 2>/dev/null; fsck.fat -n $(B)/fs_v.img >/dev/null || exit 1; done; \
	rm -f $(B)/fs_t.img $(B)/fs_v.img; echo "fstest: ok"

test: all charsets fstest
	sh tests/portcheck.sh
	$(PYTHON) tests/mk09_test.py
	$(PYTHON) tests/jwp_test.py
	$(PYTHON) tests/fpga_mk09.py

clean:
	rm -rf build build_euc build_sjis

.PHONY: all run clean charsets test fstest
