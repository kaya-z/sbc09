*******************************************
* NPty - SCF driver for the simulator's network ports (OS-9 Net)
*
* The port is a PTY style ACIA (control/status at V.PORT, data at V.PORT+1)
* whose other end is a TCP connection of the host (telnet server, Hayes
* modem or fixed connection, see src/net.c and docs/OS9NET.md).
*
* Status register:  b0 receive data ready      b1 transmitter ready (always)
*                   b2 no carrier (/DCD)       b7 hang up event
* A hang up event is reported once to the reader as end of file (E$EOF) so
* that shells and tsmon terminate when the remote side disconnects; it is
* acknowledged by reading the empty data register.
* Writing bit 0 to the control register drops DTR: the host closes the
* connection.  This is done when the driver is terminated (last close).
*
* SS.Ready: B = 1 when a character is waiting or the line hung up (the next
* read then returns E$EOF), E$NotRdy otherwise.
*******************************************

         nam NPty
         ttl Network port driver

         ifp1
         use defsfile
         endc

Revision equ 1

         org V.SCF
PST      equ .

         mod PEND,PNAM,Drivr+Objct,Reent+Revision,PENT,PST
         fcb READ.+WRITE.
PNAM     fcs /NPty/

PENT     lbra INIT
         lbra READ
         lbra WRITE
         lbra GETSTA
         lbra PUTSTA
         lbra TERM

INIT     clrb
         rts

* READ: wait for a character, report a hang up as end of file
READ     ldx V.PORT,u
readlp   ldb ,x
         bitb #$01
         bne readbyte
         bitb #$80
         bne hangup
         pshs x
         ldx #1
         os9 F$Sleep
         puls x
         ldx >D.Proc
         ldb P$Signal,x
         beq rd1
         cmpb #S$Intrpt
         bhi rd1
         coma
         rts
rd1      ldx V.PORT,u
         bra readlp
readbyte lda 1,x
         clrb
         rts
hangup   lda 1,x             acknowledge the event
         comb
         ldb #E$EOF
         rts

* WRITE: the host side never blocks
WRITE    ldx V.PORT,u
         sta 1,x
ok       clrb
         rts

GETSTA   ldx PD.Rgs,y
         lda R$B,x
         cmpa #SS.Ready
         bne gsta1
         ldx V.PORT,u
         ldb ,x
         bitb #$81            data waiting, or a hang up to report as EOF
         beq notrdy
         ldx PD.Rgs,y
         lda #1
         sta R$B,x            B = number of characters (at least 1)
         clrb
         rts
notrdy   comb
         ldb #E$NotRdy
         rts
gsta1    cmpa #SS.EOF
         beq ok
         comb
         ldb #E$UnkSVC
         rts

PUTSTA   comb
         ldb #E$UnkSVC
         rts

* TERM: drop DTR so that the host hangs up
TERM     ldx V.PORT,u
         lda #$01
         sta ,x
         clrb
         rts

         emod
PEND     equ *
