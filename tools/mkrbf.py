#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mkrbf.py - create an OS-9 RBF disk image (a "floppy") holding some host files.

    mkrbf.py [-s KBYTES] [-n NAME] -o disk.dsk [name=hostfile | hostfile ...]

The image has 256 byte sectors, an allocation bitmap with one sector per bit, a root directory
and the given files, each stored contiguously (one segment, so a program can seek in it cheaply).
The geometry fields (18 sectors per track, double sided) are those of the 180K images of the
simulator; the drivers only use the total number of sectors.
"""
import argparse, os, struct, sys, time

SECTOR = 256


def build(files, kbytes=720, name='DISK', spare=128):
    if not kbytes:                           # auto: the files, the directory and `spare` K of free space
        need = sum((os.path.getsize(p) + SECTOR - 1) // SECTOR + 1 for _, p in files) + 64
        kbytes = (need * SECTOR // 1024 + spare + 63) // 64 * 64
    total = kbytes * 1024 // SECTOR
    mapbytes = (total + 7) // 8
    mapsecs = (mapbytes + SECTOR - 1) // SECTOR
    if total >= 1 << 24:
        raise SystemExit('image too large for RBF')
    rootfd = max(18, mapsecs + 1)
    dirsecs = 2
    nxt = rootfd + 1 + dirsecs
    img = bytearray(total * SECTOR)
    bitmap = bytearray(mapsecs * SECTOR)

    def use(first, count):
        for s in range(first, first + count):
            if s >= total:
                raise SystemExit('disk full (%d K): enlarge it with -s' % kbytes)
            bitmap[s // 8] |= 0x80 >> (s % 8)

    now = time.localtime()
    date5 = bytes([now.tm_year - 1900, now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min])
    date3 = date5[:3]

    use(0, nxt)
    for b in range(total, mapbytes * 8):           # bits beyond the end of the disk
        bitmap[b // 8] |= 0x80 >> (b % 8)

    def fd_sector(att, size, segs, links=1):
        fd = bytearray(SECTOR)
        fd[0] = att
        fd[3:8] = date5
        fd[8] = links
        fd[9:13] = struct.pack('>I', size)
        fd[13:16] = date3
        for i, (lsn, cnt) in enumerate(segs):
            fd[16 + 5 * i:16 + 5 * i + 5] = lsn.to_bytes(3, 'big') + struct.pack('>H', cnt)
        return fd

    def put(first, data):
        img[first * SECTOR:first * SECTOR + len(data)] = data

    entries = []
    for fname, path in files:
        data = open(path, 'rb').read()
        nsec = (len(data) + SECTOR - 1) // SECTOR
        if nsec > 0xFFFF:
            raise SystemExit('%s: file too large for one segment' % fname)
        fdlsn = nxt
        data_lsn = nxt + 1
        use(fdlsn, 1 + nsec)
        nxt += 1 + nsec
        put(fdlsn, fd_sector(0x0B, len(data), [(data_lsn, nsec)] if nsec else []))
        put(data_lsn, data)
        entries.append((fname, fdlsn))
    use(0, nxt)

    dirdata = bytearray(dirsecs * SECTOR)
    def dent(i, nm, lsn):
        b = nm.encode('latin-1')
        e = bytearray(32)
        e[:len(b)] = b
        e[len(b) - 1] |= 0x80
        e[29:32] = lsn.to_bytes(3, 'big')
        dirdata[i * 32:i * 32 + 32] = e
    dent(0, '..', rootfd)
    dent(1, '.', rootfd)
    if len(entries) + 2 > dirsecs * 8:
        raise SystemExit('too many files')
    for i, (nm, lsn) in enumerate(entries):
        dent(2 + i, nm, lsn)
    put(rootfd, fd_sector(0xBF, (len(entries) + 2) * 32, [(rootfd + 1, dirsecs)], links=2))
    put(rootfd + 1, dirdata)

    lsn0 = bytearray(SECTOR)
    lsn0[0:3] = total.to_bytes(3, 'big')
    lsn0[3] = 18
    lsn0[4:6] = struct.pack('>H', mapbytes)
    lsn0[6:8] = struct.pack('>H', 1)
    lsn0[8:11] = rootfd.to_bytes(3, 'big')
    lsn0[11:13] = b'\0\0'
    lsn0[13] = 0xFF
    lsn0[14:16] = struct.pack('>H', (os.getpid() ^ int(time.time())) & 0xFFFF)
    lsn0[16] = 2
    lsn0[17:19] = struct.pack('>H', 18)
    lsn0[26:31] = date5
    nm = name.encode('latin-1')[:32]
    lsn0[31:31 + len(nm)] = nm
    lsn0[31 + len(nm) - 1] |= 0x80
    put(0, lsn0)
    put(1, bitmap)
    return bytes(img)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='*', help='name=hostfile or hostfile (the OS-9 name is the host base name)')
    ap.add_argument('-s', '--size', type=int, default=720, help='disk size in kilobytes (default 720; 0 = as small as the files allow, plus 128K free)')
    ap.add_argument('-n', '--name', default='DISK', help='volume name')
    ap.add_argument('-o', '--output', required=True)
    a = ap.parse_args(argv)
    files = []
    for f in a.files:
        nm, _, path = f.partition('=') if '=' in f else (os.path.basename(f), '', f)
        files.append((nm, path or f))
    img = build(files, a.size, a.name)
    with open(a.output, 'wb') as out:
        out.write(img)
    print('%s: %d K, %d file(s)' % (a.output, len(img) // 1024, len(files)))


if __name__ == '__main__':
    main()
