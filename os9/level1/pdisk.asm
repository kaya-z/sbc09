*********************************************
* Rdisk
*  A driver for a Ram disk!
* Modified for os9l1 emulator by Soren Roug 2001
*

         nam Pdisk
         ttl A Device Driver for a RAM Disk

         ifp1
         use defsfile
         endc

***********************
* Edition History

*  #   date    Comments
* -- -------- ----------------------------------------------

Revision equ 1
NumDrvs  set 2 Number of drives

         org Drvbeg
         rmb NumDrvs*DrvMem
RAMSTA   equ .

         mod RAMEND,RAMNAM,Drivr+Objct,Reent+Revision,RAMENT,RAMSTA
         fcb   $FF         mode byte

RAMNAM   fcs /Pdisk/

RAMENT   lbra INIT
         lbra READ
         lbra WRITE
         lbra GETSTA
         lbra PUTSTA
         lbra TERM

*****************************
* INIT
*  Set up the v09 disk

INIT     ldb #NumDrvs Set no drives to 2
* Setup drive tables
         stb   V.NDRV,u     save it
         lda   #$FF
         leax  DRVBEG,u     point to drive table start
L0111    sta   DD.TOT+1,x
         sta   <V.TRAK,x
         leax  <DRVMEM,x
         decb  
         bne   L0111
         clrb
INITXIT  rts

SETUPDT  lda   <PD.DRV,y      Get the drive number
         ldu   V.PORT,u
         sta   1,u    drive number
         stb   2,u    msb of lsn
         tfr   x,d
         sta   3,u
         stb   4,u
         ldd   PD.BUF,y
         sta   5,u     buffer address
         stb   6,u
         rts

*****************************
* READ
*  read a sector from disk
*  Entry: U = Static Storage
*         Y = Path Descriptor
*         B = MSB of LSN
*         X = LSB's of LSN
*  Exit: 256 byte sector in PD.BUF buffer
*
READ     pshs u,x,b         S: LSN msb(0) LSN lsw(1) static storage(3)
         bsr  SETUPDT
         lda  #$81
         sta  ,u   // perform io
         ldb  ,u   // return status
         beq  rd1
         orcc #1            error: carry set, B = code
         bra  rdx
* An RBF driver has to copy the first DD.SIZ bytes of LSN 0 into the drive
* table: RBF takes DD.BIT, DD.MAP, DD.DIR ... from there.  Without this the table
* stays empty and RBF loops forever when it computes allocation sizes (every
* operation that creates or extends a file).
rd1      tst  ,s            LSN 0?
         bne  rdok
         ldx  1,s
         bne  rdok
         ldu  3,s           our static storage
         lda  <PD.DRV,y
         ldb  #DRVMEM
         mul
         leax DRVBEG,u
         leax d,x           drive table of this drive
         ldu  PD.BUF,y      the sector just read
         ldb  #DD.SIZ
rdcp     lda  ,u+
         sta  ,x+
         decb
         bne  rdcp
rdok     clrb
rdx      leas 1,s           drop saved B, keep the result in B
         puls x,u,pc

*****************************
* WRITE
*  Write a sector to disk
* Entry: U = Static Storage
*        Y = Path Descriptor
*        B = MSB of LSN
*        X = LSB's of LSN
*        PD.Buf = Sector to write
*
WRITE    bsr  SETUPDT
         lda #$55
         sta  ,u   // perform io
         ldb  ,u   // return status
WRIT99   rts

**************************
* GETSTA
*  get device status
*
GETSTA
Unknown  comb
         clrb
         rts

**************************
* PUTSTA
*  Set device Status
*
PUTSTA   
PUTSTA90 clrb
         rts

*****************************
* TERM
*  terminate Driver
*
TERM     clrb
         rts

         emod
RAMEND   equ *

