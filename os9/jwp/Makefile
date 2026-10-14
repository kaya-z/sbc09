# SPDX-License-Identifier: MIT
# jwp - Japanese word processor for OS-9 (written in C for CMOC)
#
# The prebuilt module (jwp) is part of the distribution, so CMOC is only needed
# to change the program.  CMOC (>= 0.1.100) needs lwtools (lwasm, lwlink) on the
# PATH; tools/install_cmoc.sh builds both into a prefix of your choice:
#     tools/install_cmoc.sh $HOME/cmoc
#     make -C os9/jwp PATH=$HOME/cmoc/bin:$PATH
CMOC     ?= cmoc
CMOCOPTS  = --os9 --add-os9-stack-space=1200

all: jwp jwp-l2

msgs.h: genmsgs.py
	python3 genmsgs.py > $@

jwp: jwp.c msgs.h
	@if command -v $(CMOC) >/dev/null 2>&1; then \
	    echo "$(CMOC) $(CMOCOPTS) -o jwp jwp.c"; \
	    $(CMOC) $(CMOCOPTS) -o jwp jwp.c || exit 1; \
	else \
	    echo "cmoc not found: keeping the prebuilt jwp module"; test -f jwp; \
	fi

# Level 2 has a 64K address space per process: bigger text buffer
jwp-l2: jwp.c msgs.h
	@if command -v $(CMOC) >/dev/null 2>&1; then \
	    echo "$(CMOC) $(CMOCOPTS) -DBUFSZ=26000 -o jwp-l2 jwp.c"; \
	    $(CMOC) $(CMOCOPTS) -DBUFSZ=26000 -o jwp-l2 jwp.c || exit 1; \
	else \
	    echo "cmoc not found: keeping the prebuilt jwp-l2 module"; test -f jwp-l2; \
	fi

# test build: verifies the incremental screen layout against a layout from scratch
# after every key (used by tests/jwp_test.py when present)
jwp_chk: jwp.c msgs.h
	$(CMOC) $(CMOCOPTS) -DSELFCHECK -o jwp_chk jwp.c

clean:
	rm -f *.o *.s *.lst *.map *.link jwp_chk

.PHONY: all clean
