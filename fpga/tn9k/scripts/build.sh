#!/bin/sh
# SPDX-License-Identifier: MIT
# Build the bitstream for the Tang Nano 9K with the Gowin IDE (command line).
#
#   [HDMI=0] fpga/tn9k/scripts/build.sh [fuzix|os9|v09] [IDE_DIR]
#
# With HDMI=0 the HDMI screen (the console on a monitor, 640x480, 80x25) is left out (result: sbc09_ROM_nohdmi.fs).
#
# fuzix  the FUZIX boot ROM (256 bytes at $FF00; the disk is read from the card)       [default]
# os9    NitrOS-9 Level 1 (os9/os9v1.rom, 16 KB at $C000)
# os9boot  NitrOS-9 Level 1 booted from the card: the ROM (8 KB at $E000) holds the kernel, Init, the booter, IOMan and SCF, the rest
#          comes from the boot file OS9Boot of the card (os9/OS9.dsk has it; make -C os9 bootdisk)
# ramtest  a RAM test of the PSRAM path for the real board (scripts/ramtest.s); prints its results on the UART
# bootcheck  the FUZIX boot ROM that prints checksums of what it loaded (scripts/bootcheck.s, bootcheck_expect.py)
# os9euc, os9sjis   the same with the EUC-JP / Shift_JIS console ROM (os9v1_euc.rom, os9v1_sjis.rom)
# v09    the monitor ROM of the reference machine (os9/v09.rom, 32 KB at $8000, FIRQ timer)
#
# Result: fpga/tn9k/build/ROM/sbc09_ROM.fs   (program it with: openFPGALoader -b tangnano9k FILE)
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
ROM=${1:-fuzix}
IDE=${2:-${GOWIN_IDE:-/usr/local/IDE}}
case $ROM in
fuzix)  BIN=$ROOT/fuzix/images/bootrom.bin;  BASE="16'hFF00"; AW=8;  FREE=0; CS=1 ;;
os9boot) BIN=$ROOT/os9/os9v1boot.rom;       BASE="16'hE000"; AW=13; FREE=0; CS=1 ;;
os9)    BIN=$ROOT/os9/os9v1.rom;             BASE="16'hC000"; AW=14; FREE=0; CS=1 ;;
os9euc) BIN=$ROOT/os9/os9v1_euc.rom;         BASE="16'hC000"; AW=14; FREE=0; CS=2 ;;
os9sjis) BIN=$ROOT/os9/os9v1_sjis.rom;       BASE="16'hC000"; AW=14; FREE=0; CS=3 ;;
cputest) BIN=$HERE/build/cputest.bin;         BASE="16'hF000"; AW=12; FREE=0; CS=0 ;;
bootcheck) BIN=$HERE/build/bootcheck.bin;       BASE="16'hFC00"; AW=10; FREE=0; CS=0 ;;
ramtest) BIN=$HERE/build/ramtest.bin;                         BASE="16'hFC00"; AW=10; FREE=0; CS=0 ;;
v09)    BIN=$ROOT/os9/v09.rom;               BASE="16'h8000"; AW=15; FREE=1; CS=0 ;;
*) echo "unknown ROM $ROM"; exit 1 ;;
esac
HDMI=${HDMI:-1}
SUF=
if [ "$HDMI" = 0 ]; then SUF=_nohdmi; fi
if [ -n "$PH_DIV" ]; then SUF=${SUF}_ph$PH_DIV; fi          # PH_DIV=8: E = clock / 32 (default 9: / 36)
if [ "${DEBUG:-0}" != 0 ]; then SUF=${SUF}_debug; fi        # DEBUG=1: the UART pin carries the state of the machine (rtl/dbg_uart.v)
B=$HERE/build/$ROM$SUF
mkdir -p "$B"
python3 "$HERE/scripts/mkfont.py" -o "$B/font8x16.hex" > /dev/null
python3 "$HERE/scripts/mkkanji.py" -o "$B/kanji.bin" > /dev/null
if [ "$ROM" = cputest ]; then python3 "$HERE/scripts/mkcputest.py" "$HERE/scripts/cputest.s"; fi
if [ "$ROM" = ramtest ] || [ "$ROM" = bootcheck ] || [ "$ROM" = cputest ]; then lwasm --format=raw -o "$BIN" "$HERE/scripts/$ROM.s" || exit 1; fi
python3 "$HERE/scripts/bin2hex.py" "$BIN" > "$B/rom.hex"

# the date of the build starts the clock (there is no battery)
set -- $(date +"%Y %m %d %H %M %S")
cat > "$B/tn9k_cfg.vh" <<EOF
\`define CLK_HZ 67500000
\`define PH_DIV ${PH_DIV:-9}
\`define ROM_BASE $BASE
\`define ROM_AW $AW
\`define FREERUN $FREE
\`define RTC_YEAR $(( $1 - 1900 ))
\`define RTC_MONTH $(echo $2 | sed 's/^0//')
\`define RTC_DAY $(echo $3 | sed 's/^0//')
\`define RTC_HOUR $(echo $4 | sed 's/^0//')
\`define RTC_MIN $(echo $5 | sed 's/^0//')
\`define RTC_SEC $(echo $6 | sed 's/^0//')
\`define HDMI_ROWS 25
\`define HDMI_CHARSET ${HDMI_CHARSET:-$CS}
\`define KANJI_LOAD $( [ "$HDMI" != 0 ] && echo 1 || echo 0 )
EOF
if [ "$HDMI" != 0 ]; then echo '`define HDMI 1' >> "$B/tn9k_cfg.vh"; fi
if [ "${DEBUG:-0}" != 0 ]; then echo '`define DEBUG_TX 1' >> "$B/tn9k_cfg.vh"; fi
if [ "${DEBUG:-0}" = 3 ]; then echo '`define DEBUG_FLASH0 1' >> "$B/tn9k_cfg.vh"; fi    # DEBUG=3: DEBUG=2, and the loader reads the flash at offset 0x16
if [ "${DEBUG:-0}" = 5 ]; then echo '`define DEBUG_DISK 1' >> "$B/tn9k_cfg.vh"; fi    # DEBUG=5: the status field shows the number of disk commands and the low byte of the last sector
if [ "${DEBUG:-0}" = 4 ]; then echo '`define DEBUG_X1 1' >> "$B/tn9k_cfg.vh"; fi    # DEBUG=4: the status field shows the last word of a kanji lookup
if [ "${DEBUG:-0}" = 2 ] || [ "${DEBUG:-0}" = 3 ]; then echo '`define DEBUG_KANJI 1' >> "$B/tn9k_cfg.vh"; fi   # DEBUG=2: the status field shows the kanji loader (ok, done, failed attempts, first byte read)

# the pin constraints and the timing constraints of all parts go into one file each (the tool reads only the last file of a kind)
cat "$HERE/constraints/tn9k.cst" > "$B/pins.cst"
if [ "$HDMI" != 0 ]; then cat "$HERE/constraints/hdmi.cst" >> "$B/pins.cst"; fi
cat "$HERE/constraints/tn9k.sdc" > "$B/timing.sdc"
if [ "$HDMI" != 0 ]; then cat "$HERE/constraints/hdmi.sdc" >> "$B/timing.sdc"; fi

cat > "$B/build.tcl" <<EOF
set_device -name GW1NR-9C GW1NR-LV9QN88PC6/I5
add_file -type verilog $HERE/rtl/tn9k_top.v
add_file -type verilog $HERE/rtl/tn9k_pll.v
add_file -type verilog $HERE/rtl/sbc09_soc.v
add_file -type verilog $HERE/rtl/acia_uart.v
add_file -type verilog $HERE/rtl/timer_rtc.v
add_file -type verilog $HERE/rtl/sd_disk.v
add_file -type verilog $HERE/rtl/psram_arb.v
add_file -type verilog $HERE/rtl/dbg_uart.v
add_file -type verilog $HERE/rtl/kanji_loader.v
$( [ "$HDMI" != 0 ] && for f in hdmi_text hdmi_term hdmi_video hdmi_phy hdmi_decode hdmi_glyph psram_xfer tmds_enc; do echo "add_file -type verilog $HERE/rtl/$f.v"; done )
add_file -type verilog $HERE/vendor/mc6809/mc6809i.v
add_file -type verilog $HERE/vendor/psram/psram_controller.v
add_file -type cst $B/pins.cst
add_file -type sdc $B/timing.sdc
set_option -top_module tn9k_top
set_option -verilog_std sysv2017
set_option -include_path "$B;$HERE/rtl"
set_option -output_base_name sbc09_$ROM$SUF
set_option -use_sspi_as_gpio 1
set_option -use_mspi_as_gpio 1
set_option -use_ready_as_gpio 1
set_option -use_done_as_gpio 1
set_option -use_cpu_as_gpio 1
set_option -rw_check_on_ram 1
run all
exit
EOF
cd "$B"
# the ROM image is read by \$readmemh from the working directory
env -u DISPLAY QT_QPA_PLATFORM=offscreen "$IDE/bin/gw_sh" build.tcl 2>&1 | tee build.log | grep -v "^qt\." 
# a net used before its declaration, or without a driver, passes the Verilator simulation but not the Gowin tool (the log says so)
if grep -E "Undeclared symbol|does not have a driver" build.log | grep -v "O_psram_ck_n\|psram_controller" ; then echo "WARNING: see the lines above (build.log)"; fi
ls -l "$B/impl/pnr/"*.fs
