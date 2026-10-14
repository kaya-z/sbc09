********************************************************************
* Login - minimal replacement for tsmon on network terminals
*
* tsmon /n1 forks "login" when the first character arrives on the line.
* The NitrOS-9 login needs a password file; this one greets the user and
* chains to the shell, which is what is wanted on a private simulator.
********************************************************************

         nam   Login
         ttl   Network terminal login

         ifp1
         use   defsfile
         endc

tylg     set   Prgrm+Objct
atrv     set   ReEnt+rev
rev      set   $01
edition  set   1

         mod   eom,name,tylg,atrv,start,size

         org   0
         rmb   200
size     equ   .

name     fcs   /Login/
         fcb   edition

hello    fcc   "OS-9 NitrOS-9 on the simulator - network terminal"
         fcb   C$CR
shname   fcs   "Shell"
param    fcb   C$CR

start    leax  hello,pcr
         lda   #1
         ldy   #80
         os9   I$WritLn
         lda   #Prgrm+Objct
         clrb
         leax  shname,pcr
         ldy   #1
         leau  param,pcr
         os9   F$Chain
         os9   F$Exit

         emod
eom      equ   *
         end
