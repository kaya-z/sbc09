#!/bin/sh
# SPDX-License-Identifier: MIT
# run_cfg.sh NAME FBDIV IDIV ODIV PSDA FREQ   -> builds the author's PSRAM test with that PLL setting, loads it, prints the result
NAME=$1; FB=$2; ID=$3; OD=$4; PS=$5; FQ=$6
S=${PSRAM_TEST_SRC:?set PSRAM_TEST_SRC to the src/ directory of the author's psram-tang-nano-9k test}
D=${SCAN_DIR:-/tmp/psramscan}/$NAME
mkdir -p $D/src && cp -r $S/* $D/src/
python3 - "$D" $FB $ID $OD $PS $FQ <<'PY'
import sys,re
d,fb,idv,od,ps,fq=sys.argv[1:7]
p=d+'/src/gowin_rpll/gowin_rpll.v'
s=open(p).read()
s=re.sub(r'defparam rpll_inst.FBDIV_SEL = \d+;','defparam rpll_inst.FBDIV_SEL = %s;'%fb,s,count=1)
s=re.sub(r'defparam rpll_inst.IDIV_SEL = \d+;','defparam rpll_inst.IDIV_SEL = %s;'%idv,s,count=1)
s=re.sub(r'defparam rpll_inst.ODIV_SEL = \d+;','defparam rpll_inst.ODIV_SEL = %s;'%od,s,count=1)
s=re.sub(r'defparam rpll_inst.PSDA_SEL = "\d+";','defparam rpll_inst.PSDA_SEL = "%s";'%ps,s,count=1)
open(p,'w').write(s)
p=d+'/src/psram_test_top.v'
s=open(p).read()
s=re.sub(r'localparam FREQ = \d+_\d+_\d+;','localparam FREQ = %s;'%fq,s,count=1)
open(p,'w').write(s)
PY
cat > $D/build.tcl <<EOF
set_device -name GW1NR-9C GW1NR-LV9QN88PC6/I5
add_file -type verilog $D/src/gowin_rpll/gowin_rpll.v
add_file -type verilog $D/src/psram_controller.v
add_file -type verilog $D/src/psram_test_top.v
add_file -type verilog $D/src/uart_tx.v
add_file -type cst $D/src/tang9k.cst
set_option -top_module memory_test
set_option -include_path $D/src
set_option -output_base_name t
set_option -use_mspi_as_gpio 1
set_option -use_sspi_as_gpio 1
set_option -use_ready_as_gpio 1
set_option -use_done_as_gpio 1
set_option -use_cpu_as_gpio 1
run all
exit
EOF
cd $D && env -u DISPLAY QT_QPA_PLATFORM=offscreen /usr/local/IDE/bin/gw_sh build.tcl > build.log 2>&1
stty -F /dev/ttyUSB1 115200 raw -echo cs8 -cstopb -parenb -crtscts clocal
rm -f $D/out.bin
(timeout 14 cat /dev/ttyUSB1 > $D/out.bin 2>/dev/null &)
sleep 0.5
timeout 60 openFPGALoader -b tangnano9k $D/impl/pnr/t.fs > $D/load.log 2>&1
sleep 13
echo "== $NAME fb=$FB id=$ID od=$OD psda=$PS freq=$FQ"
tr -c '[:print:]\n' '.' < $D/out.bin | grep -a -o "Initializing.*\|Writing\.\.\.\|Reading\.\.\.\|FAIL[^L]*\|PASS[^L]*\|Latency counters.*\|Test.*\|[Ss]uccess.*\|Done.*" | tail -4
