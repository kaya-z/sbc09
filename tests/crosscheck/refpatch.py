#!/usr/bin/env python3
"""Build an instrumented copy of the reference simulator (sbc09 v09 / v09c by
Shinji Kono, GPL v2) for instruction-by-instruction cross checks.

usage: refpatch.py SBC09_SRC_DIR OUT_DIR

The copy differs from the original in these ways only:
  * the 50 Hz timer is driven by the number of executed instructions
    (DETPERIOD, default 5000) instead of the host clock; CWAI / SYNC wait loops
    advance the same counter,
  * keyboard input can be injected at instruction counts (DETTYPE/DETSTART/DETGAP),
    the run stops after DETMAX instructions, the RTC returns a fixed time,
  * the flag quirks of the original engine that disagree with the MC6809 data
    sheet are corrected so that a trace comparison shows only real problems:
    ADD/ADC set H, ASL/ASR leave H alone, TST clears V,
  * VRBF returns E$PNNF for a missing file.
"""
import os, re, shutil, subprocess, sys

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
for f in ('engine.c', 'io.c', 'v09.c', 'v09.h', 'trace.c', 'vdisk.c', 'd09.c'):
    shutil.copy(os.path.join(src, f), out)

def rw(name, fn):
    p = os.path.join(out, name)
    s = open(p, encoding='latin1').read()
    s = fn(s)
    open(p, 'w', encoding='latin1').write(s)

def io(s):
    s = s.replace("int timer = 0;", "int timer = 0;\nint det_timer_on = 0;", 1)
    s = s.replace("   setitimer(ITIMER_REAL, &timercontrol, NULL);\n}\n\nvoid stop_timer", "   det_timer_on = 1;\n}\n\nvoid stop_timer", 1)
    s = s.replace("void stop_timer(void) {", "void stop_timer(void) {\n    det_timer_on = 0;\n    return;", 1)
    s = s.replace("            setitimer(ITIMER_REAL, &timercontrol, NULL);\n}", "            ;\n}")
    s = s.replace("    time_t tm = time(0);\n    struct tm *t = localtime(&tm);", "    time_t tm = 1790000000;\n    struct tm *t = localtime(&tm);")
    s = s.replace("int char_input(void) {", """extern unsigned long det_count;
static int det_pos;
int char_input(void) {
        if (getenv("DETTYPE")) {
            const char *t = getenv("DETTYPE");
            unsigned long st = strtoul(getenv("DETSTART"),0,0), gp = strtoul(getenv("DETGAP"),0,0);
            if (t[det_pos] && det_count >= st + det_pos*gp) { int ch = (unsigned char)t[det_pos++]; if (ch=='\\\\' && t[det_pos]=='r') { ch='\\r'; det_pos++; } return ch; }
            return EOF;
        }""", 1)
    return s
rw('io.c', io)

def eng(s):
    s = s.replace("#include <stdio.h>\n#include <unistd.h>", "#include <stdio.h>\n#include <stdlib.h>\n#include <unistd.h>", 1)
    s = s.replace("#define USLEEP 1000", """#define USLEEP 1000
extern int det_timer_on;
unsigned long det_count;
static unsigned long det_period = 5000;
#define DETTICK { det_count++; if (det_timer_on && det_count % det_period == 0) { irq = timerirq; mem[IOPAGE+0x30] |= 0x10; } if (getenv("DETMAX") && det_count > strtoul(getenv("DETMAX"),0,0)) exit(0); }""", 1)
    s = s.replace(" for(;;){\n  if(attention) {", """ det_period = getenv("DETPERIOD") ? strtoul(getenv("DETPERIOD"),0,0) : 5000;
 for(;;){
  det_count++;
  if (det_timer_on && det_count % det_period == 0) { attention = 1; irq = timerirq; mem[IOPAGE+0x30] |= 0x10; }
  if (getenv("DETMAX") && det_count > strtoul(getenv("DETMAX"),0,0)) { exit(0); }
  if(attention) {""", 1)
    s = s.replace("""                      do     usleep(USLEEP); /* Wait for IRQ */
                      while(!attention && !((irq==1&&!(iccreg&0x10))||(irq==2&&!(iccreg&0x040))));""", """                      do     DETTICK /* Wait for IRQ */
                      while(!((irq==1&&!(iccreg&0x10))||(irq==2&&!(iccreg&0x040))));""")
    s = s.replace("""                         do  usleep(USLEEP); /* Wait for IRQ */
                         while(!irq && !attention);""", """                         do  DETTICK /* Wait for IRQ */
                         while(!irq);""")
    # ASL: no half carry; ASR: no H copy
    s = re.sub(r"(case 0x(?:08|48|58|68|78): /\*ASL[^\n]*\n(?:[^\n]*\n)?[^\n]*?)SETSTATUSH\(", r"\1SETSTATUS(", s)
    s = s.replace("if(tb&0x10)SEH else CLH ", "")
    # TST clears V
    s = s.replace("/*TST direct*/ DIRECT tb=mem(eaddr);SETNZ8(tb) break;", "/*TST direct*/ DIRECT tb=mem(eaddr);SETNZ8(tb) CLV break;")
    s = s.replace("/*TSTA*/  SETNZ8(iareg) break;", "/*TSTA*/  SETNZ8(iareg) CLV break;")
    s = s.replace("/*TSTB*/  SETNZ8(ibreg) break;", "/*TSTB*/  SETNZ8(ibreg) CLV break;")
    s = s.replace("/*TST indexed*/  tb=mem(eaddr);SETNZ8(tb) break;", "/*TST indexed*/  tb=mem(eaddr);SETNZ8(tb) CLV break;")
    s = s.replace("/*TST ext*/ EXTENDED tb=mem(eaddr);SETNZ8(tb) break;", "/*TST ext*/ EXTENDED tb=mem(eaddr);SETNZ8(tb) CLV break;")
    # ADD / ADC set H
    s = re.sub(r"(case 0x(?:89|8B|99|9B|A9|AB|B9|BB|C9|CB|D9|DB|E9|EB|F9|FB): /\*AD[DC][AB][^\n]*\n(?:[^\n]*\n)?[^\n]*?)SETSTATUS\(", r"\1SETSTATUSH(", s)
    return s
rw('engine.c', eng)

def vd(s):
    s = s.replace("""                struct stat buf;
                if (stat(pd->name,&buf)!=0) break;
                if ((buf.st_mode & S_IFMT) == S_IFDIR) {""", """                struct stat buf;
                if (stat(pd->name,&buf)!=0) { *breg = 0xd8; pd->use = 0; break; }
                if ((buf.st_mode & S_IFMT) == S_IFDIR) {""")
    s = s.replace("""                struct stat buf;
                if (stat(dm.name,&buf)!=0) break;
                if ((buf.st_mode & S_IFMT) != S_IFDIR) break;""", """                struct stat buf;
                if (stat(dm.name,&buf)!=0) { *breg = 0xd8; break; }
                if ((buf.st_mode & S_IFMT) != S_IFDIR) { *breg = 0xd8; break; }""")
    return s
rw('vdisk.c', vd)

cf = ['gcc', '-O1', '-g', '-w', '-DUSE_TERMIOS', '-DBIG_ENDIAN', '-DUSE_VDISK']
os.chdir(out)
subprocess.check_call(cf + ['-DNO_MAIN', '-c', 'd09.c', '-o', 'd09.o'])
subprocess.check_call(cf + ['-o', 'refv09', 'v09.c', 'engine.c', 'io.c', 'd09.o', 'trace.c', 'vdisk.c'])
subprocess.check_call(cf + ['-DIOPAGE=0xff80', '-DUSE_MMU=1', '-o', 'refv09c', 'v09.c', 'engine.c', 'io.c', 'd09.o', 'trace.c', 'vdisk.c'])
print('built', os.path.join(out, 'refv09'), os.path.join(out, 'refv09c'))
