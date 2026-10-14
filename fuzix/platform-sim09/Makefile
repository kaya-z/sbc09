# SPDX-License-Identifier: GPL-2.0-only
export CPU = 6809
export ENDIANFLAG = "-X"

CSRCS = devices.c main.c devtty.c devhd.c devrtc.c

C3SRCS = libc.c

CDSRCS = discard.c

DSRCS = ../../dev/tinydisk.c

DDSRCS = ../../dev/tinydisk_discard.c

ASRCS = crt0.s sim09.s
ASRCS += tricks.s commonmem.s

COBJS = $(CSRCS:.c=$(BINEXT))
C3OBJS = $(C3SRCS:.c=$(BINEXT))
CDOBJS = $(CDSRCS:.c=$(BINEXT))
AOBJS = $(ASRCS:.s=$(BINEXT))
DOBJS = $(patsubst ../../dev/%.c,%.o, $(DSRCS))
DDOBJS = $(patsubst ../../dev/%.c,%.o, $(DDSRCS))
OBJS  = $(COBJS) $(C3OBJS) $(CDOBJS) $(AOBJS) $(DOBJS) $(DDOBJS)

CROSS_CCOPTS += -I../../dev/

all: $(OBJS)

$(COBJS): %$(BINEXT): %.c
	$(CROSS_CC) $(CROSS_CCOPTS) $(CROSS_CC_SEG2) -c $<

$(C3OBJS): %$(BINEXT): %.c
	$(CROSS_CC) $(CROSS_CCOPTS) $(CROSS_CC_SEG2) -c $<

$(CDOBJS): %$(BINEXT): %.c
	$(CROSS_CC) $(CROSS_CCOPTS) $(CROSS_CC_SEGDISC) -c $<

tinydisk.o: ../../dev/tinydisk.c
	$(CROSS_CC) $(CROSS_CCOPTS) $(CROSS_CC_SEG2) -c $<

$(DDOBJS): %$(BINEXT): ../../dev/%.c
	$(CROSS_CC) $(CROSS_CCOPTS) $(CROSS_CC_SEGDISC) -c $<

$(AOBJS): %$(BINEXT): %.s
	$(CROSS_AS) $(ASOPTS) $< -o $*.o

clean:
	rm -f *.o *~ fuzix.tmp fuzix.img

image:
	$(CROSS_LD) -o ../../fuzix.bin -Map=../../fuzix.map --script=fuzix.link --oformat=decb \
	crt0.o commonmem.o \
	sim09.o discard.o ../../simple.o \
	../../start.o ../../version.o ../../cpu-6809/lowlevel-6809.o ../../memalloc_none.o \
	tricks.o main.o ../../timer.o ../../kdata.o devices.o \
	tinydisk.o tinydisk_discard.o devhd.o \
	../../devio.o ../../filesys.o ../../process.o ../../inode.o ../../syscall_fs.o \
	../../syscall_proc.o ../../syscall_other.o ../../mm.o ../../swap.o \
	../../tty.o ../../devsys.o ../../usermem.o ../../syscall_fs2.o ../../syscall_exec16.o \
	devtty.o devrtc.o libc.o ../../syscall_fs3.o ../../syscall_exec.o ../../blk512.o
	python3 mkimage.py ../../fuzix.bin fuzix.img
