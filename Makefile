# SPDX-License-Identifier: MIT
# MC6809 / OS-9 simulator - top level Makefile (Debian 12, gcc, python3)
#
#   make            build the simulator (sim09), the OS-9 tools, ROM images and commands
#   make test       run the self tests (CPU, OS-9 system tests, Japanese, jwp, OS-9 Net)
#   make test-diff  compare the CPU core with sbc09's v09s.c   (needs V09S_SRC)
#   make test-cross instruction-by-instruction cross check against the reference
#                   simulator (needs REF_SBC09, the sbc09 source tree)
CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
SRC      = src/m6809.c src/machine.c src/console.c src/net.c src/vdisk.c src/dbg.c src/dis6809.c src/main.c
OBJ      = $(SRC:.c=.o)

REF_SBC09 ?= ../sbc09_shinji
V09S_SRC  ?= $(REF_SBC09)/src/v09s.c

all: sim09 sim09c os9

sim09c: sim09          # the Level 2 machine is selected by the program name too
	ln -sf sim09 sim09c

sim09: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c src/*.h
	$(CC) $(CFLAGS) -c -o $@ $<

tools: tools/a09 tools/os9mod tools/make9rom

tools/a09: tools/a09.c tools/os9crc.c
	$(CC) -O2 -w -o $@ tools/a09.c tools/os9crc.c
tools/os9mod: tools/crc.c tools/os9mod.c tools/os9.h
	$(CC) -O2 -w -o $@ tools/crc.c tools/os9mod.c
tools/make9rom: tools/makerom.c
	$(CC) -O2 -w -o $@ tools/makerom.c

os9: tools
	$(MAKE) -C os9

# SKK dictionary disks for jwp (needs SKK-JISYO.L: apt install skkdic)
skk:
	$(MAKE) -C os9/skk

# ------------------------------------------------------------------ tests
test: all test-cpu test-os9 test-jwp test-dhry test-net test-fuzix test-hdmi test-fpga

test-cpu: tests/cpu_test
	tests/cpu_test

tests/cpu_test: tests/cpu_test.c src/m6809.c src/m6809.h
	$(CC) -O1 -g -Wall -o $@ tests/cpu_test.c src/m6809.c

test-diff: tests/diff_v09s
	tests/diff_v09s 300

tests/diff_v09s: tests/diff_v09s.c src/m6809.c src/m6809.h
	$(CC) -O1 -g -w -DV09S_SRC='"$(abspath $(V09S_SRC))"' -o $@ tests/diff_v09s.c src/m6809.c

test-os9: all
	LC_ALL=C python3 tests/os9_test.py

test-jwp: all
	LC_ALL=C python3 tests/jwp_test.py

test-dhry: all
	LC_ALL=C python3 tests/dhry_test.py

test-net: all
	LC_ALL=C python3 tests/net_test.py --level 1 --port 16801
	LC_ALL=C python3 tests/net_test.py --level 2 --port 16802
	LC_ALL=C python3 tests/net_test.py --level 1 --kanji euc --port 16803
	LC_ALL=C python3 tests/net_test.py --level 2 --kanji sjis --port 16804

test-fuzix: all
	LC_ALL=C python3 tests/fuzix_test.py

test-hdmi:
	LC_ALL=C python3 tests/hdmi_test.py
	LC_ALL=C python3 tests/hdmi_kanji_test.py

test-fpga:
	LC_ALL=C python3 tests/fpga_test.py

test-cross: all
	tests/crosscheck/crosscheck.sh $(REF_SBC09)

clean:
	rm -f src/*.o sim09 sim09c tests/cpu_test tests/diff_v09s
	-$(MAKE) -C os9 clean
	-$(MAKE) -C os9/skk clean
	-$(MAKE) -C fpga/tn9k/sim clean
	-$(MAKE) -C fpga/tn9k/sim -f Makefile.hdmi clean
	rm -rf fpga/tn9k/build

distclean: clean
	rm -f tools/a09 tools/os9mod tools/make9rom



.PHONY: all tools os9 skk test test-cpu test-diff test-os9 test-jwp test-dhry test-net test-fuzix test-hdmi test-fpga test-cross clean distclean
