OS9SRC=../nitros9-code
SRCDIR=$(OS9SRC)/level2/modules
SRCCMD=$(OS9SRC)/level1/cmds
SRCCMD2=$(OS9SRC)/level2/cmds

NETDESC = n1 n2 n3 n1_euc n2_euc n3_euc n1_sjis n2_sjis n3_sjis

A09 = ../../tools/a09 -I $(OS9SRC)/defs/

ALL = ioman pdisk init os9p1 os9p2 os9p3_perr os9p4_regdump pipe pipeman pipeman_named piper rbf scf term term_euc term_sjis npty $(NETDESC) pty d0 d1 clock vector boot shell dir mdir sysgo v0 vrbf

all : $(ALL)
	cd cmds ; make

clean : 
	cd cmds ; make clean
	rm -rf $(ALL) *.lst *.dSYM

LST = -l $@.lst
pdisk : pdisk.asm
	$(A09) pdisk.asm -o pdisk $(LST) $(LST)

boot : boot.asm
	$(A09) boot.asm -o boot $(LST)

sysgo : sysgo.asm
	$(A09) sysgo.asm -o sysgo $(LST)

init : init.asm
	$(A09) init.asm -o $@ $(LST)

vector : vector.asm
	$(A09) vector.asm -o vector $(LST)

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

v0 : v0.asm
	$(A09) $@.asm -o $@ $(LST)

clock :  
	$(A09) clock.asm -o clock $(LST)

pty :
	$(A09) ../level1/pty.asm -o pty $(LST)

shell :
	$(A09) ${SRCCMD}/shell_21.asm -o shell $(LST)

mdir :
	$(A09) ${SRCCMD2}/mdir.asm -o mdir $(LST)

dir : ../kanji/dir.asm
	$(A09) ../kanji/dir.asm -o dir $(LST)

os9p1 :
	$(A09) -I $(OS9SRC)/level1/modules/kernel/  ${SRCDIR}/kernel/krn.asm -o os9p1 $(LST)

os9p2 :
	$(A09) ${SRCDIR}/kernel/krnp2.asm -o os9p2 $(LST)

os9p3_perr :
	$(A09) ${SRCDIR}/krnp3_perr.asm -o os9p3_perr $(LST)

os9p4_regdump :
	$(A09) ${SRCDIR}/krnp4_regdump.asm -o os9p4_regdump $(LST)

ioman : ioman.asm
	$(A09) ioman.asm -o ioman $(LST)

pipe :
	$(A09) $(OS9SRC)/level1/modules/pipe.asm  -o $@ $(LST)

pipeman :
	$(A09) $(OS9SRC)/level1/modules/pipeman.asm  -o $@ $(LST)

pipeman_named :
	$(A09) ${SRCDIR}/pipeman_named.asm -o pipeman_named $(LST)

piper :
	$(A09) $(OS9SRC)/level1/modules/piper.asm  -o $@ $(LST)

rbf :
	$(A09) ${SRCDIR}/rbf.asm -o rbf $(LST)

vrbf : vrbf.asm
	$(A09) vrbf.asm -o vrbf $(LST)

scf : ../kanji/scf.asm
	$(A09) ../kanji/scf.asm  -o $@ $(LST)



# OS-9 Net: network port driver and descriptors (see ../net)
npty : ../net/npty.asm
	$(A09) ../net/npty.asm -o npty $(LST)

$(NETDESC) : ../net/nport.asm
	$(A09) -D L2 -D P$(word 1,$(subst _, ,$(subst n,,$@))) $(if $(findstring _euc,$@),-D EUC) $(if $(findstring _sjis,$@),-D SJIS) ../net/nport.asm -o $@ -l $@.lst
