# SPDX-License-Identifier: MIT
# Verilator simulation of the sim09 FPGA system.
#
#   make              build/obj/Vsim_top   (FUZIX boot ROM at $FF00; ROM=os9 for the OS-9 Level 1 ROM)
#   make run          boot FUZIX from the disk image in a script (needs fuzix/images)
#   make DEBUG=1      with a VCD trace (-t file)
ROOT   := ../../..
ROM    ?= fuzix
ifeq ($(ROM),fuzix)
ROMBIN  = $(ROOT)/fuzix/images/bootrom.bin
ROMBASE = 65280
ROMAW   = 8
FREERUN = 0
else ifeq ($(ROM),os9)
ROMBIN  = $(ROOT)/os9/os9v1.rom
ROMBASE = 49152
ROMAW   = 14
FREERUN = 0
else ifeq ($(ROM),os9boot)
ROMBIN  = $(ROOT)/os9/os9v1boot.rom
ROMBASE = 61440
ROMAW   = 12
FREERUN = 0
else ifeq ($(ROM),bootcheck)
ROMBIN  = ../build/bootcheck.bin
ROMBASE = 64512
ROMAW   = 10
FREERUN = 0
else ifeq ($(ROM),cputest)
ROMBIN  = ../build/cputest.bin
ROMBASE = 61440
ROMAW   = 12
FREERUN = 0
else ifeq ($(ROM),ramtest)
ROMBIN  = ../build/ramtest.bin
ROMBASE = 64512
ROMAW   = 10
FREERUN = 0
else ifeq ($(ROM),v09)
ROMBIN  = $(ROOT)/os9/v09.rom
ROMBASE = 32768
ROMAW   = 15
FREERUN = 1
endif
B      := build/$(ROM)$(if $(HDMI),-hdmi,)
SRC     = ../rtl/sbc09_soc.v ../rtl/acia_uart.v ../rtl/timer_rtc.v ../rtl/sd_disk.v ../rtl/psram_arb.v ../rtl/kanji_loader.v \
          ../vendor/mc6809/mc6809i.v psram_model.v sim_top.v
HDMISRC = ../rtl/hdmi_text.v ../rtl/hdmi_term.v ../rtl/hdmi_video.v ../rtl/hdmi_decode.v ../rtl/hdmi_glyph.v ../rtl/psram_xfer.v ../rtl/tmds_enc.v
HDMIOPT = $(if $(HDMI),+define+HDMI '+define+FONT_FILE="$(abspath ../rtl/font8x16.hex)"' +define+HDMI_CHARSET=$(CHARSET) -CFLAGS -DHDMI,)
PH_DIV ?= 9
CHARSET ?= 1                       # the character set of the HDMI screen: 0 raw, 1 UTF-8, 2 EUC-JP, 3 Shift_JIS
TRACE  := $(if $(DEBUG),--trace -CFLAGS -DVM_TRACE=1,)

$(B)/obj/Vsim_top: $(SRC) $(HDMISRC) tb.cpp $(B)/rom.hex ../rtl/font8x16.hex
	verilator --cc --exe --build -O2 -j 8 -I../rtl --top-module sim_top -Mdir $(B)/obj $(TRACE) \
	  -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -Wno-UNOPTFLAT -Wno-CASEX -Wno-VARHIDDEN \
	  -GROM_BASE=$(ROMBASE) -GROM_AW=$(ROMAW) -GROM_FILE='"$(abspath $(B)/rom.hex)"' -GFREERUN=$(FREERUN) -GPH_DIV=$(PH_DIV) \
	  -CFLAGS "-O2" $(HDMIOPT) $(SRC) $(if $(HDMI),$(HDMISRC),) $(abspath tb.cpp) -o Vsim_top >/dev/null

$(B)/rom.hex: $(ROMBIN) ../scripts/bin2hex.py
	mkdir -p $(B)
	python3 ../scripts/bin2hex.py $(ROMBIN) > $@

run: $(B)/obj/Vsim_top
	cd $(ROOT) && gzip -dc fuzix/images/sim09-fuzix.dsk.gz > /tmp/fz_sd.img
	$(B)/obj/Vsim_top -d /tmp/fz_sd.img -s fuzix_boot.txt

clean:
	rm -rf build
