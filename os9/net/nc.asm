* SPDX-License-Identifier: MIT
********************************************************************
* NC - network terminal / netcat style client for OS-9 Net
*
* Usage: nc <port-device> [host:port]
*
*   nc /n2 www.example.org:80    dial through the Hayes style modem on /n2
*                                (-net 2=modem on the simulator) and relay
*                                keyboard and screen to the TCP connection
*   nc /n3                       just relay to the line (listen/tcp ports)
*
* Keyboard CR is sent as CR LF; LF from the network is shown as CR LF.
* Press ^C (or ^\) to disconnect.  The hang up is done by closing
* the path (the NPty driver drops DTR when the last path is closed).
*
* Part of the MC6809/OS-9 simulator (see README / docs/OS9NET.md).
********************************************************************

         nam   NC
         ttl   Network terminal client

         ifp1
         use   defsfile
         endc

tylg     set   Prgrm+Objct
atrv     set   ReEnt+rev
rev      set   $01
edition  set   1

EKO      equ   PD.EKO-PD.OPT
ALF      equ   PD.ALF-PD.OPT
UPC      equ   PD.UPC-PD.OPT
PAU      equ   PD.PAU-PD.OPT
INT      equ   PD.INT-PD.OPT

         mod   eom,name,tylg,atrv,start,size

         org   0
netpath  rmb   1
done     rmb   1
last     rmb   1
idle     rmb   1
argp     rmb   2
kopts    rmb   32          original keyboard (path 0) options
nopts    rmb   32          original options of the network path
wopts    rmb   32          working copy
buf      rmb   132
out      rmb   8
cmdbuf   rmb   120
         rmb   250
size     equ   .

name     fcs   /NC/
         fcb   edition

mUse     fcc   "Usage: nc <port-device> [host:port]"
         fcb   C$CR
mFail    fcc   "nc: could not connect: "
         fcb   C$CR
mClosed  fcc   "nc: connection closed"
         fcb   C$CR
mConn    fcc   "nc: connected, ^C to disconnect"
         fcb   C$CR
cATE0    fcc   "ATE0V1"
         fcb   C$CR
cATDT    fcc   "ATDT"

icpt     lda   #1
         sta   done,u
         rti

start    stx   argp,u
         lda   ,x
         cmpa  #C$CR
         lbeq  usage
         leax  icpt,pcr
         os9   F$Icpt
         ldx   argp,u
         lda   #UPDAT.
         os9   I$Open               open the port device
         lbcs  exit
         sta   netpath,u
sk       lda   ,x+
         cmpa  #C$SPAC
         beq   sk
         leax  -1,x
         stx   argp,u               start of host:port (or CR)
         clr   done,u
         clr   last,u

* keyboard and net path: no echo, no auto line feed, no upper case
         clra
         leax  kopts,u
         lbsr   getopt
         lda   netpath,u
         leax  nopts,u
         lbsr   getopt
         clra
         leax  kopts,u
         lbsr   setraw
         lda   netpath,u
         leax  nopts,u
         lbsr   setraw

         ldx   argp,u
         lda   ,x
         cmpa  #C$CR
         beq   relay

* ---- dial ----
         leax  cATE0,pcr
         lbsr   sendcr
         lbsr  waitok
         leay  cmdbuf,u
         leax  cATDT,pcr
         ldb   #4
cpd      lda   ,x+
         sta   ,y+
         decb
         bne   cpd
         ldx   argp,u
cpa      lda   ,x+
         sta   ,y+
         cmpa  #C$CR
         bne   cpa
         leax  cmdbuf,u
         lbsr  writeline
wres     lbsr  readline             wait for CONNECT / NO CARRIER / BUSY / ERROR
         bcs   dialerr
         stx   argp,u               remember the line for the error message
         cmpa  #'C
         beq   conn
         cmpa  #'N
         beq   dialfail
         cmpa  #'B
         beq   dialfail
         cmpa  #'E
         beq   dialfail
         bra   wres
conn     leax  mConn,pcr
         lbsr  puts
         bra   relay
dialfail leax  mFail,pcr
         lbsr  putsn
         ldx   argp,u
         lbsr  puts
         ldb   #E$Read
         lbra   cleanup
dialerr  lbra   cleanup

* ---- relay keyboard <-> network ----
relay    clr   idle,u
         tst   done,u
         lbne   finish
* keyboard -> network
         clra
         ldb   #SS.Ready
         os9   I$GetStt
         bcs   nokey
         clra
         leax  buf,u
         ldy   #1
         os9   I$Read
         bcs   nokey
         lda   buf,u
         cmpa  #3                   ^C or ^\ : disconnect (SCF does not turn them into
         lbeq  finish               signals for raw reads, so nc looks for them itself)
         cmpa  #$1C
         lbeq  finish
         cmpa  #C$CR
         bne   k1
         lda   netpath,u
         leax  buf,u
         ldy   #1
         os9   I$Write              CR
         lda   #C$LF
         sta   buf,u
k1       lda   netpath,u
         leax  buf,u
         ldy   #1
         os9   I$Write
         inc   idle,u
nokey
* network -> screen
         ldb   #64
         pshs  b
net1     lda   netpath,u
         ldb   #SS.Ready
         os9   I$GetStt
         bcs   netdone
         lda   netpath,u
         leax  buf,u
         ldy   #1
         os9   I$Read
         bcs   neterr
         inc   idle,u
         lda   buf,u
         ldb   last,u
         sta   last,u
         cmpa  #C$LF
         bne   n2
         cmpb  #C$CR
         beq   n2
         lda   #C$CR
         sta   out,u
         lda   #C$LF
         sta   out+1,u
         ldy   #2
         bra   n3
n2       sta   out,u
         ldy   #1
n3       lda   #1
         leax  out,u
         os9   I$Write
         dec   ,s
         bne   net1
netdone  leas  1,s
         tst   idle,u
         lbne  relay
         ldx   #1
         os9   F$Sleep              nothing to do: wait a tick
         lbra  relay
neterr   leas  1,s
         cmpb  #E$EOF
         bne   finish
         lbsr  putsn                 new line, then the message
         leax  mClosed,pcr
         lbsr  puts
finish   clrb
cleanup  pshs  b
         clra
         leax  kopts,u
         bsr   putopt
         lda   netpath,u
         leax  nopts,u
         bsr   putopt
         lda   netpath,u
         os9   I$Close               last close drops DTR (hang up)
         puls  b
         tstb
         beq   exit
         coma
exit     os9   F$Exit

usage    leax  mUse,pcr
         bsr   puts
         ldb   #E$IllArg
         coma
         bra   exit

* ---- subroutines ----
getopt   ldb   #SS.Opt              A = path, X = buffer
         os9   I$GetStt
         rts
putopt   ldb   #SS.Opt
         os9   I$SetStt
         rts
* setraw: A = path, X = saved options; set a raw copy
setraw   pshs  a,x
         leay  wopts,u
         ldb   #32
cp32     lda   ,x+
         sta   ,y+
         decb
         bne   cp32
         clr   EKO+wopts,u
         clr   ALF+wopts,u
         clr   UPC+wopts,u
         clr   PAU+wopts,u
         lda   ,s
         leax  wopts,u
         ldb   #SS.Opt
         os9   I$SetStt
         puls  a,x,pc

puts     lda   #1                   X = CR terminated string
         ldy   #100
         os9   I$WritLn
         rts
putsn    lda   #1
         pshs  x
         leax  crstr,pcr
         ldy   #1
         os9   I$WritLn
         puls  x
         rts
crstr    fcb   C$CR

* sendcr: X = CR terminated string to the net path
sendcr   lda   netpath,u
         ldy   #80
         os9   I$WritLn
         rts
* writeline: X = line in the data area (CR terminated)
writeline lda  netpath,u
         ldy   #120
         os9   I$WritLn
         rts
* readline: read a line from the net path into buf with raw 1 byte reads
* (ReadLn would run the SCF line editor); LFs are dropped.
* exit: carry set on error, else A = first character, X -> line, carry clear
readline leax  buf,u
         stx   out,u               ... out+0/1 = write pointer (reuse the out area)
rl1      lda   netpath,u
         leax  buf+129,u
         ldy   #1
         os9   I$Read
         bcs   rl9
         lda   buf+129,u
         cmpa  #C$LF
         beq   rl1
         ldx   out,u
         sta   ,x+
         stx   out,u
         cmpa  #C$CR
         beq   rl2
         leay  buf+127,u
         pshs  y
         cmpx  ,s++
         blo   rl1
rl2      leax  buf,u
         lda   ,x
         andcc #$FE
rl9      rts

* waitok: read lines until one starts with 'O' (OK)
waitok   lbsr  readline
         bcs   wo9
         cmpa  #'O
         bne   waitok
wo9      rts

         emod
eom      equ   *
         end
