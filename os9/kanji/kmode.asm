* SPDX-License-Identifier: MIT
********************************************************************
* KMode - select the Japanese character encoding of the terminal
*
* Usage: kmode [off|utf8|euc|sjis]
*
* The SCF line editor (os9/kanji/scf.asm) erases a whole multibyte
* character, and echoes the erase sequence once per display column,
* depending on PD.PAR of the path:
*      0 off   1 UTF-8   2 EUC-JP   3 Shift_JIS (CP932)
* Without an argument the current setting is shown.
*
* Part of the MC6809/OS-9 simulator (see README).
********************************************************************

         nam   KMode
         ttl   Japanese terminal encoding

         ifp1
         use   defsfile
         endc

tylg     set   Prgrm+Objct
atrv     set   ReEnt+rev
rev      set   $01
edition  set   1

PAROFS   equ   PD.PAR-PD.OPT

         mod   eom,name,tylg,atrv,start,size

         org   0
opts     rmb   32
         rmb   200
size     equ   .

name     fcs   /KMode/
         fcb   edition

mOff     fcc   /Kanji mode: off (single byte)/
         fcb   C$CR
mUtf     fcc   /Kanji mode: UTF-8/
         fcb   C$CR
mEuc     fcc   /Kanji mode: EUC-JP/
         fcb   C$CR
mSjis    fcc   /Kanji mode: Shift_JIS/
         fcb   C$CR
mUse     fcc   /Usage: kmode [off|utf8|euc|sjis]/
         fcb   C$CR

start    pshs  x                  parameter pointer
         clra                     path 0 = keyboard
         ldb   #SS.Opt
         leax  opts,u
         os9   I$GetStt
         lbcs  exit
skip     ldx   ,s
sk1      lda   ,x+
         cmpa  #C$SPAC
         beq   sk1
         leax  -1,x
         cmpa  #C$CR
         beq   show
         ora   #$20               lower case
         ldb   #0
         cmpa  #'o
         beq   set
         ldb   #1
         cmpa  #'u
         beq   set
         ldb   #2
         cmpa  #'e
         beq   set
         ldb   #3
         cmpa  #'s
         beq   set
         leax  mUse,pcr
         bsr   print
         ldb   #E$IllArg
         coma
         bra   exit
set      stb   opts+PAROFS,u
         clra
         ldb   #SS.Opt
         leax  opts,u
         os9   I$SetStt
         bcs   exit
show     ldb   opts+PAROFS,u
         leax  mOff,pcr
         cmpb  #1
         bne   sh1
         leax  mUtf,pcr
sh1      cmpb  #2
         bne   sh2
         leax  mEuc,pcr
sh2      cmpb  #3
         bne   sh3
         leax  mSjis,pcr
sh3      bsr   print
         clrb
exit     os9   F$Exit

print    lda   #1
         ldy   #80
         os9   I$WritLn
         rts

         emod
eom      equ   *
         end
