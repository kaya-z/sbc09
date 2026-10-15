
OS9SRC = ../nitros9-code
SRCDIR = $(OS9SRC)/level1/modules
SRCCMD = $(OS9SRC)/level1/cmds
SRCDIR2 = $(OS9SRC)/level2/modules

NETDESC = n1 n2 n3 n1_euc n2_euc n3_euc n1_sjis n2_sjis n3_sjis

A09 = ../../tools/a09 -I $(OS9SRC)/defs/

MOD =  boot pdisk init init_tn9k os9p1 os9p1_tn9k os9p2 ioman pipe piper rbf scf term term_euc term_sjis npty $(NETDESC) pty d0 d1 vrbf v0 clock shell dir mdir sysgo shell

all : ${MOD} 
	cd cmds; make

clean : 
	cd cmds; make clean
	rm -f $(MOD) *.lst krn.asm

LST = -l $@.lst

# our own module for sbc09
# CoCoOS9's level1/os9p1.asm has wrong vector value, use our own

pdisk : pdisk.asm
	$(A09) pdisk.asm -o pdisk $(LST) 

# the disk booter (os9v1boot.rom): boot_common.asm of NitrOS-9 does the work
boot : boot.asm ${SRCDIR}/boot_common.asm
	$(A09) -I ${SRCDIR}/ boot.asm -o boot $(LST)

# the name of the machine in the boot message: init (the simulator), init_tn9k (the Tang Nano 9K FPGA board)
init : init.asm ${SRCDIR}/init.asm
	$(A09) -D SIM09 ${SRCDIR}/init.asm -o $@ $(LST)

init_tn9k : init.asm ${SRCDIR}/init.asm
	$(A09) -D TN9K ${SRCDIR}/init.asm -o $@ $(LST)
#	$(A09) init.asm -o init $(LST)

pty : pty.asm
	$(A09) pty.asm -o pty $(LST)

term : pty-dd.asm
	$(A09) pty-dd.asm -o term $(LST)

term_euc : pty-dd.asm
	$(A09) -D EUC pty-dd.asm -o term_euc -l term_euc.lst

term_sjis : pty-dd.asm
	$(A09) -D SJIS pty-dd.asm -o term_sjis -l term_sjis.lst

d0 : d0.asm
	$(A09) d0.asm -o d0 $(LST)

d1 : d1.asm
	$(A09) d1.asm -o d1 $(LST)

clock :  clock.asm
	$(A09) clock.asm -o clock $(LST)

vrbf :  
	$(A09) ../level2/vrbf.asm -o $@  $(LST)

v0 :  
	$(A09) v0.asm -o $@  $(LST)

# os9 level1 moduels

shell : $(SRCCMD)/shell_21.asm 
	$(A09) $< -o $@ $(LST)

mdir : ${SRCCMD}/mdir.asm
	$(A09) $< -o $@ $(LST)

dir : ../kanji/dir.asm
	$(A09) ../kanji/dir.asm -o dir $(LST)

os9p1 :
	cat  ${OS9SRC}//defs/coco.d ${SRCDIR}/kernel/krn.asm > krn.asm
	$(A09) -I ../nitros9-code/level1/modules/kernel/ krn.asm  -o os9p1 $(LST)

# the kernel for the FPGA board: does not probe the I/O page at $E000 (see krn.asm); used where the RAM reaches it
os9p1_tn9k : os9p1
	$(A09) -D TN9K -I ../nitros9-code/level1/modules/kernel/ krn.asm  -o os9p1_tn9k $(LST)

os9p2 :
	$(A09) ${SRCDIR}/kernel/krnp2.asm -o os9p2 $(LST)

sysgo :
	$(A09) sysgo.asm -o sysgo $(LST)

ioman :
	$(A09) ${SRCDIR}/ioman.asm -o ioman $(LST)

pipe :
	$(A09) ${SRCDIR}/pipe.asm -o pipe $(LST)

piper :
	$(A09) ${SRCDIR}/piper.asm -o piper $(LST)

rbf :
	$(A09) ${SRCDIR}/rbf.asm -o rbf $(LST)

scf : ../kanji/scf.asm
	$(A09) ../kanji/scf.asm -o scf $(LST)




# OS-9 Net: network port driver and descriptors (see ../net)
npty : ../net/npty.asm
	$(A09) ../net/npty.asm -o npty $(LST)

$(NETDESC) : ../net/nport.asm
	$(A09) -D P$(word 1,$(subst _, ,$(subst n,,$@))) $(if $(findstring _euc,$@),-D EUC) $(if $(findstring _sjis,$@),-D SJIS) ../net/nport.asm -o $@ -l $@.lst
