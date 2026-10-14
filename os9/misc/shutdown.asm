* SPDX-License-Identifier: MIT
********************************************************************
* Shutdown - power off the simulated machine
*
* Writes $A5 to the power control register (I/O base + $0F: $E00F on
* Level 1, $FF8F on Level 2).  The simulator then exits cleanly (disk
* images and files of /v0 are already on the host, every write goes
* through).  Assemble with -D L2 for Level 2.
*
* Part of the MC6809/OS-9 simulator (see README).
********************************************************************

         nam   Shutdown
         ttl   Power off the simulator

         ifp1
         use   defsfile
         endc

tylg     set   Prgrm+Objct
atrv     set   ReEnt+rev
rev      set   $00
edition  set   1

PWR      set   $E00F
         IFNE  L2
PWR      set   $FF8F
         ENDC

         mod   eom,name,tylg,atrv,start,size

         org   0
         rmb   100
size     equ   .

name     fcs   /Shutdown/
         fcb   edition

msg      fcc   "System halted."
         fcb   C$CR

start    leax  msg,pcr
         lda   #1
         ldy   #40
         os9   I$WritLn
         lda   #$A5
         sta   >PWR
         clrb
         os9   F$Exit

         emod
eom      equ   *
         end
