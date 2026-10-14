********************************************************************
* Boot - disk booter of the sim09 / sbc09 "v09" machine (Level 1)
*
* SPDX-License-Identifier: MIT
* Copyright (c) 2026 sim09 contributors
*
* Reads the boot file (OS9Boot) of drive 0 through the DMA disk port at
* $E040 (the one the PDisk driver uses; on the FPGA it is the SD card)
* and hands it to the kernel.  The reading of LSN 0 and of the boot file
* is done by boot_common.asm (NitrOS-9, used unchanged): this file only
* provides HWInit, HWTerm and HWRead.
*
* The port:  +0 command ($81 = read a sector) / result (0 = ok)
*            +1 drive   +2..+4 LSN (24 bits)   +5,+6 address of the buffer

         nam   Boot
         ttl   Boot from the v09 disk port

         ifp1
         use   defsfile
         endc

tylg     set   Systm+Objct
atrv     set   ReEnt+rev
rev      set   $00
edition  set   1

         mod   eom,name,tylg,atrv,start,size

* NOTE: these are U-stack offsets (boot_common.asm uses them)
seglist  rmb   2                pointer to segment list
blockloc rmb   2                pointer to memory requested
blockimg rmb   2                duplicate of the above
bootsize rmb   2                size in bytes
LSN0Ptr  rmb   2                in memory LSN0 pointer
size     equ   .

name     fcs   /Boot/
         fcb   edition

LSN24BIT equ   1
FLOPPY   equ   0

* HWInit - nothing to do (the disk port is always ready)
*   Entry: Y = hardware address
*   Exit:  Carry Clear = OK
HWInit   clrb
         rts

* HWTerm
HWTerm   clrb
         rts

* HWRead - read a 256 byte sector
*   Entry: Y = hardware address
*          B = bits 23-16 of LSN
*          X = bits 15-0  of LSN
*          blockloc,u = ptr to 256 byte sector
*   Exit:  X = ptr to data (i.e. ptr in blockloc,u)
*          Carry Clear = OK, Set = Error (B = error)
HWRead   stb   2,y              LSN, bits 23-16
         tfr   x,d
         sta   3,y
         stb   4,y
         clr   1,y              drive 0
         ldd   blockloc,u
         sta   5,y              address of the buffer
         stb   6,y
         lda   #$81
         sta   ,y               read (the CPU waits for the transfer)
         ldb   ,y               result
         bne   HWRErr
         ldx   blockloc,u
         clrb
         rts
HWRErr   orcc  #1
         rts

         use   boot_common.asm

Address  fdb   $E040
         emod
eom      equ   *
