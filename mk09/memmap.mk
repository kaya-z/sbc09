# SPDX-License-Identifier: MIT
# memmap.mk - the memory map of the sbc09 machine (64KB, flat, no MMU): the SIZES of the slots.
# tools/genmap.py lays the slots out one after the other and writes build/memmap.h (C and lwasm)
# and build/memmap.gen.mk (the addresses for this Makefile).
#
#   $0000-$00FF  page 0 (not used; a null pointer stays invalid)
#   $0100-$010F  interrupt vector jump table (the boot ROM vectors point here)
#   $0200-$02FF  sector buffer of the boot ROM (free after the boot)
#   $0300-       system code: kernel, TTY, DISK, FS, PM (one slot each, read from the boot image)
#   TPA_BASE-    transient program area: the programs started by the shell; the shell itself
#                sits at the top of it (SH_BASE .. TPA_END) and is taken out of memory by PM
#                when a bigger program needs the room
#   $E000-$E1FF  I/O page
#   $E200-$FEFF  data, bss and stacks of the system modules (one slot each)
#   $FF00-$FFFF  boot ROM and hardware vectors
# All numbers are hexadecimal without prefix.

VEC_BASE   = 0100
SYS_BASE   = 0300
DATA_BASE  = E200
TPA_END    = E000

# size of the code slots (the build checks that the code fits)
K_CSZ      = 0840
TTY_CSZ    = 08A0
DISK_CSZ   = 02B0
FS_CSZ     = 27D0
PM_CSZ     = 0A50

# size of the data slots (rwdata + bss + stack)
K_DSZ      = 0200
TTY_DSZ    = 01C0
DISK_DSZ   = 0190
FS_DSZ     = 0780
PM_DSZ     = 0260

# The memory above the I/O page has more room than the data need: the code of these modules is
# put there (behind the data slots) instead of into the low memory, which the programs need.
HIGH       = DISK PM

# the shell slot at the top of the transient area (file + bss + stack + start-up block)
SH_SIZE    = 1800
