#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of the FPGA system (fpga/tn9k/, docs/FPGA.md) in the Verilator simulation.

The RTL of the Tang Nano 9K design - the cycle accurate 6809 core, the ACIA on a 115200 baud UART, the
timer and clock, the disk port on a microSD card in SPI mode (a card model in the test bench), the 64 KB
RAM behind the PSRAM controller interface - runs the boot ROMs and the disk images of the simulator:

  fuzix   FUZIX boots from the card (SDHC), logs in, runs commands, shuts down (power off); a second boot
          finds the file and a clean file system
  sdsc    the same boot with a byte addressed (SDSC) card
  os9     NitrOS-9 Level 1: the shell, the disk /d0 on the card, the module directory
  os9boot NitrOS-9 Level 1 booted from the card (4 KB ROM: kernel, Init and the booter; the rest is the boot file OS9Boot)
  nocard  no card: the boot ROM finds no disk and powers the machine off
  hdmi    FUZIX with the HDMI screen: what the guest writes to the console is on the screen (decoded from the TMDS symbols),
          including Japanese text (UTF-8): the kanji image is copied from the flash to the PSRAM and read back for the display

Needs verilator and a C++ compiler; the FUZIX tests need fuzix/images (fuzix/build.sh).  A boot takes
a few minutes of host time (the machine runs 15 seconds of its own time).    Run:  tests/fpga_test.py [-v]
"""
import concurrent.futures, gzip, os, re, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIMDIR = os.path.join(ROOT, 'fpga', 'tn9k', 'sim')
VERBOSE = '-v' in sys.argv
fails = total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     '))


if not shutil.which('verilator'):
    print('SKIP: verilator is not installed')
    sys.exit(0)

sys.path.insert(0, os.path.join(ROOT, 'fpga', 'tn9k', 'scripts'))
FZ_ROM = os.path.join(ROOT, 'fuzix', 'images', 'bootrom.bin')
FZ_DSK = os.path.join(ROOT, 'fuzix', 'images', 'sim09-fuzix.dsk.gz')
have_fuzix = os.path.exists(FZ_ROM) and os.path.exists(FZ_DSK)
OS9_DSK = os.path.join(ROOT, 'os9', 'OS9.dsk')
have_os9 = os.path.exists(os.path.join(ROOT, 'os9', 'os9v1.rom')) and os.path.exists(OS9_DSK)
have_os9boot = os.path.exists(os.path.join(ROOT, 'os9', 'os9v1boot.rom')) and have_os9


KANJI = os.path.join(tempfile.mkdtemp(prefix='fpgat_kj_'), 'kanji.bin')
subprocess.run([sys.executable, os.path.join(ROOT, 'fpga', 'tn9k', 'scripts', 'mkkanji.py'), '-o', KANJI], check=True, capture_output=True)


def build(rom):
    hdmi = rom.endswith('-hdmi')
    rom = rom.replace('-hdmi', '')
    r = subprocess.run(['make', '-C', SIMDIR, 'ROM=' + rom] + (['HDMI=1'] if hdmi else []), capture_output=True, text=True)
    exe = os.path.join(SIMDIR, 'build', rom + ('-hdmi' if hdmi else ''), 'obj', 'Vsim_top')
    if r.returncode or not os.path.exists(exe):
        print(r.stdout[-2000:], r.stderr[-2000:])
        sys.exit('build of the simulation failed (ROM=%s)' % rom)
    return exe


def run(exe, script, disk=None, extra=(), clks=4000000000, timeout=900):
    """script: lines for tb; returns (output, return code)"""
    d = tempfile.mkdtemp(prefix='fpgat_')
    sp = os.path.join(d, 's.txt')
    with open(sp, 'w', encoding='utf-8') as f:
        f.write('\n'.join(script) + '\n')
    cmd = [exe, '-s', sp, '-c', str(clks), '-kanji', KANJI] + list(extra)
    if disk:
        cmd += ['-d', disk]
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout)
        out = (p.stdout + p.stderr).decode('latin1')
        rc = p.returncode
    except subprocess.TimeoutExpired:
        out, rc = 'TIMEOUT', -1
    shutil.rmtree(d, ignore_errors=True)
    if VERBOSE:
        print(out)
    return out, rc


def fuzix_disk(d):
    p = os.path.join(d, 'fuzix_card.img')
    with gzip.open(FZ_DSK) as g, open(p, 'wb') as f:
        shutil.copyfileobj(g, f)
    return p


LOGIN = ['expect login:', 'send root\\r', 'expect # ']


def cmds(*cs):
    r = []
    for c in cs:
        r += ['send %s\\r' % c, 'expect # ']
    return r


def t_fuzix(exe):
    d = tempfile.mkdtemp(prefix='fpgat_fz_')
    disk = fuzix_disk(d)
    out, rc = run(exe, LOGIN + cmds('echo hello from fpga', 'echo persist > /root/p', 'ls / | cat', 'ps', 'df', 'date')
                  + ['send shutdown\\r', 'expect Halted.'], disk)
    r = [('FUZIX boots on the FPGA design: kernel banner, 64KiB RAM, partitions, root file system',
          'FUZIX version' in out and '64KiB total RAM' in out and 'hda: hda1 hda2 (swap)' in out
          and 'Mounting root fs' in out and 'OK' in out, out[:600]),
         ('the console UART works both ways: the login and the commands are echoed and run',
          'login: root' in out and re.search(r'# echo hello from fpga\r?\nhello from fpga', out) is not None, out[-800:]),
         ('processes, pipes and the swapper work (ls | cat, ps, df)',
          re.search(r'# ls / \| cat\r?\n(.*\r?\n)*?bin\r?\n', out) is not None and 'PID' in out
          and re.search(r'/dev/hda1\s+\d+', out) is not None, out[-800:]),
         ('the clock of the board is running (date prints a year >= 2026)', re.search(r'# date\r?\n.*(202[6-9]|20[3-9]\d)', out) is not None, out[-300:]),
         ('shutdown halts and the machine powers off ($E00F), exit code 0', 'Halted.' in out and 'power off' in out and rc == 0, 'rc=%s' % rc),
         ('the card was written (swap and file system)', re.search(r'(\d+) block writes', out) is not None and int(re.search(r'(\d+) block writes', out).group(1)) > 100, out[-200:])]
    out2, rc2 = run(exe, LOGIN + cmds('cat /root/p') + ['send shutdown\\r', 'expect Halted.'], disk)
    r += [('a second boot: the file written before is there', re.search(r'# cat /root/p\r?\npersist', out2) is not None, out2[-400:]),
          ('and the file system was left clean by shutdown', 'not cleanly' not in out2 and 'Halted.' in out2, out2[:900])]
    shutil.rmtree(d, ignore_errors=True)
    return r


def t_sdsc(exe):
    d = tempfile.mkdtemp(prefix='fpgat_sd_')
    disk = fuzix_disk(d)
    out, rc = run(exe, LOGIN + cmds('echo sdsc ok') + ['send shutdown\\r', 'expect Halted.'], disk, extra=['-sdsc'])
    shutil.rmtree(d, ignore_errors=True)
    return [('a byte addressed (SDSC) card works too: boot, command, shutdown',
             'sdsc ok' in out and 'Halted.' in out and rc == 0, out[-600:])]


def t_os9(exe):
    d = tempfile.mkdtemp(prefix='fpgat_os9_')
    disk = os.path.join(d, 'OS9.dsk')
    shutil.copy(OS9_DSK, disk)
    out, rc = run(exe, ['wait 2000', 'send \\r', 'wait 300', 'send chx /d0/cmds\\r', 'wait 300', 'send dir /d0\\r', 'wait 4000',
                        'send mdir\\r', 'wait 3000', 'quit'], disk)
    shutil.rmtree(d, ignore_errors=True)
    return [('NitrOS-9 Level 1 boots from its ROM to the shell prompt (timer interrupts, console)',
             'NitrOS-9/6809 Level 1' in out and 'OS9:' in out, out[:600]),
            ('the disk /d0 is read from the card (dir /d0 lists OS9Boot and CMDS)', 'Directory of /d0' in out and 'OS9Boot' in out and 'CMDS' in out, out[-700:]),
            ('the module directory shows the drivers (Pdisk, VRBF, SCF, RBF, Clock)',
             all(x in out for x in ('Pdisk', 'VRBF', 'SCF', 'RBF', 'Clock')), out[-500:])]


def t_os9boot(exe):
    d = tempfile.mkdtemp(prefix='fpgat_os9b_')
    disk = os.path.join(d, 'OS9.dsk')
    shutil.copy(OS9_DSK, disk)
    out, rc = run(exe, ['wait 2000', 'send \\r', 'wait 300', 'send chx /d0/cmds\\r', 'wait 300', 'send dir /d0\\r', 'wait 4000',
                        'send mdir\\r', 'wait 3000', 'quit'], disk)
    shutil.rmtree(d, ignore_errors=True)
    mods = out[out.find('Module Directory'):]
    return [('NitrOS-9 Level 1 boots from the boot file on the card to the shell prompt',
             'NitrOS-9/6809 Level 1' in out and 'OS9:' in out, out[:600]),
            ('the disk /d0 is read (dir /d0), and the modules of the boot file are in the module directory (IOMan, RBF, SCF, Pdisk, Clock, Shell, SysGo)',
             'Directory of /d0' in out and all(x in mods for x in ('Boot', 'IOMan', 'RBF', 'SCF', 'Pdisk', 'Clock', 'Shell', 'SysGo')), out[-700:])]


def t_nocard(exe):
    out, rc = run(exe, ['wait 3000', 'quit'], None, extra=['-nosd'], clks=1500000000)
    return [('without a card the boot ROM gives up and powers off (exit code 0, no kernel banner)',
             rc == 0 and 'power off' in out and 'FUZIX' not in out, 'rc=%s\n%s' % (rc, out[-300:]))]


def t_hdmi(exe):
    from hdmi_screen import read_ppm, screen_lines, screen_unicode
    d = tempfile.mkdtemp(prefix='fpgat_hd_')
    disk = fuzix_disk(d)
    snap = os.path.join(d, 'screen.ppm')
    jp = '日本語のテキスト、ひらがなカタカナ漢字：亜唖娃阿哀愛'
    out, rc = run(exe, LOGIN + cmds('echo hello from the screen', 'ls / | cat', 'echo ' + jp) + ['snap ' + snap, 'send shutdown\\r', 'expect Halted.'], disk)
    r = [('the HDMI output is valid TMDS, DC balanced, frame after frame', re.search(r'\[hdmi: (\d+) frames, 0 invalid symbols, worst DC imbalance (\d+) bits\]', out) is not None
          and int(re.search(r'worst DC imbalance (\d+)', out).group(1)) <= 12, out[-300:])]
    if os.path.exists(snap):
        img = read_ppm(snap)
        lines = screen_lines(img)
        ulines = screen_unicode(img)
        text = '\n'.join(lines)
        r += [('the screen shows the console: the commands and what they print',
               '# echo hello from the screen' in lines and 'hello from the screen' in lines and 'bin' in lines and 'usr' in lines, text),
              ('Japanese text typed on the console (UTF-8, echoed by the tty) is on the screen as kanji: the image came from the flash, through the PSRAM',
               re.search(r'\[flash: \d+ bytes read, kanji image loaded: 1\]', out) is not None and ('# echo ' + jp) in ulines, '\n'.join(ulines[-8:]))]
    else:
        r += [('a frame of the screen was captured', False, out[-400:])]
    shutil.rmtree(d, ignore_errors=True)
    return r


jobs = []
if have_fuzix:
    jobs += [('fuzix', 'fuzix', t_fuzix), ('sdsc', 'fuzix', t_sdsc), ('nocard', 'fuzix', t_nocard), ('hdmi', 'fuzix-hdmi', t_hdmi)]
else:
    print('SKIP fuzix tests: fuzix/images is empty (run fuzix/build.sh)')
if have_os9:
    jobs += [('os9', 'os9', t_os9)]
if have_os9boot:
    jobs += [('os9boot', 'os9boot', t_os9boot)]
else:
    print('SKIP os9 test: os9/os9v1.rom or os9/OS9.dsk is missing (make)')

ONLY = [a for a in sys.argv[1:] if not a.startswith('-')]
if ONLY:
    jobs = [j for j in jobs if j[0] in ONLY]
exes = {}
for rom in sorted(set(j[1] for j in jobs)):
    exes[rom] = build(rom)

with concurrent.futures.ThreadPoolExecutor(max_workers=len(jobs) or 1) as ex:
    futs = [(name, ex.submit(fn, exes[rom])) for name, rom, fn in jobs]
    for name, f in futs:
        print('== ' + name)
        for what, cond, detail in f.result():
            check(cond, what, detail)

print('%d checks, %d failure(s)' % (total, fails))
sys.exit(1 if fails else 0)
