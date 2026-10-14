#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""OS-9 Net test: telnet terminal (tsmon on /n1), modem client (nc on /n2).

usage: net_test.py [--level 1|2] [--kanji utf8|euc|sjis]
Starts the simulator, uses its test script to start tsmon on /n1 and to run
nc on /n2, then talks to it over TCP like a telnet user / a remote server.
"""
import os, sys, socket, subprocess, tempfile, time, threading, select, argparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument('--level', type=int, default=1)
ap.add_argument('--kanji', default='utf8')
ap.add_argument('--port', type=int, default=16809)
args = ap.parse_args()

work = tempfile.mkdtemp(prefix='os9net_')
for d in ('OS9.dsk', 'WORK.dsk'):
    subprocess.check_call(['cp', os.path.join(ROOT, 'os9', d), work])
v0 = os.path.join(work, 'v0')
os.mkdir(v0)
subprocess.check_call(['cp', '-r', os.path.join(ROOT, 'os9', 'level%d' % args.level, 'cmds'), v0])

rom = 'os9v%d%s.rom' % (args.level, '' if args.kanji == 'utf8' else '_' + args.kanji)
script = os.path.join(work, 'net.txt')
open(script, 'w').write('''timeout 120000
expect "OS9:"
send "chx /v0/cmds\\r"
expect "OS9:"
send "tsmon /n1&\\r"
expect "OS9:"
send "echo READY-FOR-NET\\r"
expect "READY-FOR-NET"
expect "NEVER-MATCHES" 100000
''')

cmd = [os.path.join(ROOT, 'sim09')] + (['-2'] if args.level == 2 else []) + [
    '-rom', os.path.join(ROOT, 'os9', rom), '-0', os.path.join(work, 'OS9.dsk'),
    '-1', os.path.join(work, 'WORK.dsk'), '-v', v0, '-kanji', args.kanji,
    '-net', '1=listen:%d' % args.port, '-net', '2=modem', '-script', script]
sim = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
out = bytearray()
def pump():
    while True:
        b = sim.stdout.read1(4096)
        if not b: break
        out.extend(b)
threading.Thread(target=pump, daemon=True).start()

fails = 0
def check(cond, what):
    global fails
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond: fails += 1

def wait_for(buf, text, timeout=60):
    t = time.time() + timeout
    while time.time() < t:
        if text in bytes(buf): return True
        time.sleep(0.05)
    return False

try:
    enc = {'utf8': 'utf-8', 'euc': 'euc-jp', 'sjis': 'cp932'}[args.kanji]
    assert wait_for(out, b'echo READY-FOR-NET'), 'sim did not boot'
    time.sleep(1.0)

    # ---- telnet terminal on /n1 ----
    s = socket.create_connection(('127.0.0.1', args.port), timeout=5)
    got = bytearray()
    def rd(t=0.3):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([s], [], [], 0.05)
            if r:
                d = s.recv(4096)
                if not d: return False
                got.extend(d); end = time.time() + t
        return True
    def expect(text, timeout=40):
        t = time.time() + timeout
        while time.time() < t:
            if text in bytes(got): return True
            rd(0.2)
        return False
    rd(0.5)
    check(b'\xff\xfb\x01' in bytes(got), 'telnet negotiation (IAC WILL ECHO) sent')
    s.sendall(b'\xff\xfd\x01\xff\xfb\x1f\r\x00')            # client answers + CR NUL
    check(expect(b'network terminal'), 'tsmon/login greeted the telnet user')
    check(expect(b'OS9:'), 'shell prompt on the network terminal')
    if b'OS9:' not in bytes(got): print('DEBUG got:', bytes(got)[-300:])
    got.clear()
    s.sendall(b'echo hello from telnet\r\n')
    check(expect(b'hello from telnet\r\n') , 'command output over telnet')
    got.clear()
    jp = 'こんにちは世界'.encode('utf-8')      # telnet clients speak UTF-8; the simulator converts to the guest charset
    s.sendall(b'echo ' + jp + b'\r\n')
    check(expect(jp + b'\r\n'), 'Japanese text round trip over telnet (%s)' % args.kanji)
    got.clear()
    s.sendall(b'mdir\r\n')
    check(expect(b'Module Directory') or expect(b'Module directory'), 'mdir on the network terminal')
    # hang up: shell must see EOF, tsmon respawns for the next caller
    s.close()
    time.sleep(2)
    s = socket.create_connection(('127.0.0.1', args.port), timeout=5)
    got.clear()
    rd(0.5)
    s.sendall(b'\r')
    check(expect(b'network terminal', 40), 'second telnet session accepted after logout')
    s.close()

    # ---- modem client: nc /n2 -> a TCP server written here ----
    srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', 0)); srv.listen(1)
    sport = srv.getsockname()[1]
    # drive the OS-9 console through a second script step: use the sim's own input via a type-in file is not
    # possible here, so ask the shell on the telnet terminal to run nc on /n2
    s = socket.create_connection(('127.0.0.1', args.port), timeout=5)
    got.clear(); rd(0.5)
    s.sendall(b'\r')
    check(expect(b'OS9:', 40), 'shell prompt again')
    got.clear()
    s.sendall(b'chx /v0/cmds\r\n')
    expect(b'OS9:', 10)
    got.clear()
    s.sendall(('nc /n2 127.0.0.1:%d\r\n' % sport).encode())
    srv.settimeout(40)
    conn, _ = srv.accept()
    check(True, 'nc dialed out through the modem (TCP connection arrived)')
    conn.sendall(b'Hello from the server\r\n')
    check(expect(b'Hello from the server'), 'data from the remote server reached the OS-9 screen')
    got.clear()
    s.sendall(b'hi server\r')
    conn.settimeout(20)
    data = b''
    t = time.time() + 20
    while b'hi server' not in data and time.time() < t:
        try: data += conn.recv(100)
        except socket.timeout: break
    check(b'hi server' in data, 'keyboard input was sent to the remote server (got %r)' % data)
    conn.close()                                 # remote hangs up
    check(expect(b'connection closed', 30), 'nc reports the lost connection')
    got.clear()
    s.sendall(b'echo still alive\r\n')
    check(expect(b'still alive'), 'shell is back after nc')
    s.close()
finally:
    sim.terminate()
    time.sleep(0.3)
    sim.kill()
print('result: %d failure(s)' % fails)
if fails:
    print(out.decode('utf-8', 'replace')[-1500:])
sys.exit(1 if fails else 0)
