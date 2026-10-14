****************************************
* Network port descriptors N1 N2 N3 (OS-9 Net)
*
* Assemble with  -D P1 | -D P2 | -D P3   (port number)
*                -D L2                    (Level 2 I/O base $FF80, Level 1: $E000)
* and optionally -D EUC / -D SJIS         (terminal character set, default UTF-8)
****************************************
         ifp1
         use defsfile
         endc

         nam N1
         ttl Network port device descriptor

KMODE    set   1
         IFNE  EUC
KMODE    set   2
         ENDC
         IFNE  SJIS
KMODE    set   3
         ENDC

IOBASE   set   $E004
         IFNE  L2
IOBASE   set   $FF84
         ENDC
         IFNE  P2
IOBASE   set   IOBASE+2
         ENDC
         IFNE  P3
IOBASE   set   IOBASE+4
         ENDC

EOFCH    set   $04
RPRCH    set   $12
         IFNE  L2
EOFCH    set   $1b
RPRCH    set   $04
         ENDC

         mod PEND,PNAM,DEVIC+OBJCT,REENT+1,PMGR,PDRV

         fcb READ.+WRITE.+SHARE.
         fcb $FF IOBlock (unused)
         fdb IOBASE hardware address
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
         fcb EOFCH eof
         fcb RPRCH reprint line char
         fcb $01 duplicate last line char
         fcb $17 pause char
         fcb $03 interrupt char
         fcb $05 quit char
         fcb $08 backspace echo char
         fcb $07 bell
         fcb KMODE PD.PAR terminal encoding 1=UTF-8 2=EUC-JP 3=SJIS
         fcb $00 n/a
         fdb PNAM offset to name
         fdb $0000 offset to status routine
PNAM     equ *
         IFNE P3
         fcs "N3"
         ELSE
         IFNE P2
         fcs "N2"
         ELSE
         fcs "N1"
         ENDC
         ENDC
PMGR     fcs "SCF"
PDRV     fcs "NPty"
         emod
PEND     equ *
         end
