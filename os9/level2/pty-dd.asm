****************************************
* PTY Descriptor module
*
* Source by Soren Roug 2001
*
         ifp1
         use defsfile
         endc

         nam P1
         ttl PTY Device Descriptor
* KMODE selects the default character encoding of the terminal for the multibyte
* aware line editor (PD.PAR, see kanji/scf.asm): 1 = UTF-8, build with -D EUC for
* EUC-JP (2) or -D SJIS for Shift_JIS (3).
KMODE    set   1
         IFNE  EUC
KMODE    set   2
         ENDC
         IFNE  SJIS
KMODE    set   3
         ENDC


         mod PEND,PNAM,DEVIC+OBJCT,REENT+1,PMGR,PDRV

         fcb READ.+WRITE.+SHARE.
         fcb $FF IOBlock (unused)
         fdb $ff80 hardware address
         fcb PNAM-*-1 option byte count
         fcb $0 SCF device
         fcb $0 Case (upper & lower)
         fcb $1 Erase on backspace
         fcb $0 delete (BSE over line)
         fcb $1 echo on
         fcb $1 lf on
         fcb $0 eol null count
         fcb $0 no pause
         fcb 24 lines per page
         fcb $8 backspace
         fcb $18 delete line char
         fcb $0D end of record
         fcb $1b eof
         fcb $04 reprint line char
         fcb $01 duplicate last line char
         fcb $17 pause char
         fcb $03 interrupt char
         fcb $05 quit char
         fcb $08 backspace echo char
         fcb $07 bell
         fcb KMODE PD.PAR terminal encoding 1=UTF-8 2=EUC-JP 3=SJIS
         fcb $00 n/a
         fdb pnam offset to name
         fdb $0000 offset to status routine
pnam     fcs "TERM"
pmgr     fcs "SCF"
pdrv     fcs "PTY"
         emod
pend     equ *
         end
