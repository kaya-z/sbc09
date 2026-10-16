********************************************************************
* l2boot - the loader of the Level 2 machine (FPGA board)
*
* SPDX-License-Identifier: MIT
* Copyright (c) 2026 YUKARI Semiconductor Devices
*
* The 256 byte boot ROM (fuzix/bootrom.s) loads this code into the RAM of the Level 1 machine at $0300 and jumps to it
* (it is one record of the image on the card: tools/mkl2disk.py).  It
*   - writes $A2 to the mode register ($E00E): the machine becomes the Level 2 machine (MMU, 2 MB, I/O at $FF80);
*     the system map is the identity for the blocks 0-6, so that this code goes on,
*   - reads the ROM image of the Level 2 machine (os9/os9v2_tn9k.rom, 105 sectors from LSN IMGLSN) through the window
*     $2000-$3FFF (system map 1) into the physical blocks $3F, $40, $41, $42 ($7E000 on: where sim09 -2 puts it),
*   - writes $A3 to the mode register: the CPU is reset (the maps as the simulator has them), and runs the boot code
*     of the image at $FFD0.
* Without a card (or a failing read) it powers the machine off ($FF8F).
*
* Assemble: a09 -o l2boot l2boot.asm  (the output starts with $0300 bytes of zeros: tools/mkl2disk.py cuts them off)
********************************************************************

IMGLSN   equ   720            first sector of the image on the card
NSECT    equ   105            26878 bytes = 105 sectors

         org   $0300

start    orcc  #$50           interrupts off
         lda   #$A2
         sta   $E00E          Level 2 mode: from now on the registers are at $FF80
         ldx   #IMGLSN
         ldu   #blocks
         ldy   #NSECT
nextblk  lda   ,u+
         sta   $FFA1          system map 1 = the window $2000-$3FFF: this block
         lda   #$20           buffer address (high byte)
nextsec  clr   $FFC1          drive 0
         clr   $FFC2          LSN bits 23-16
         stx   $FFC3          LSN bits 15-0
         sta   $FFC5          the DMA address, a logical address: it goes through the map
         clr   $FFC6
         ldb   #$81
         stb   $FFC0          read the sector (the CPU waits)
         ldb   $FFC0          result: 0 = good
         bne   fail
         leax  1,x
         leay  -1,y
         beq   done
         inca                 the next 256 bytes of the window
         cmpa  #$40
         bne   nextsec
         bra   nextblk
done     lda   #$A3
         sta   $FF8E          reset the CPU: it starts the Level 2 machine
hang     bra   hang
fail     lda   #$A5
         sta   $FF8F          no card: power off
         bra   fail
blocks   fcb   $3F,$40,$41,$42
         end
