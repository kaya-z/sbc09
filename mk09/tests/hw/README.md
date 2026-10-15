# tests/hw - checks on the real Tang Nano 9K

`diag.s` / `diag.img`: a boot stream that replaces the MK09 boot image on the card (it is written to
byte 512 of the card only, the FAT is not touched).  It prints a banner on the ACIA, then the first 16 bytes
of some sectors that it reads with the disk registers, and powers the board off.  It separates the problems
of the UART and of the card from those of the kernel.

    sudo dd if=tests/hw/diag.img of=/dev/sdX bs=512 seek=1 conv=fsync     # /dev/sdX: check it!

Restore MK09 by writing the whole `build/mk09.img` again.  The UART of the board is the second of the two USB serial
ports (`/dev/ttyUSB2` with interface number 01 on the machine of the 2026-10-15 test); `openFPGALoader` may
re-enumerate them, so look at the device again after a bitstream load.
