# SPDX-License-Identifier: MIT
# Dhrystone 2.1 for OS-9 (CMOC).  The prebuilt module is part of the distribution; CMOC is only
# needed to change the program (tools/install_cmoc.sh, then make -C os9/dhry PATH=...).
# The module name is the output file name: build as `-o dhry`, not with a path.
CMOC     ?= cmoc
CMOCOPTS  = --os9 --add-os9-stack-space=1000

all: dhry

dhry: dhry.c
	@if command -v $(CMOC) >/dev/null 2>&1; then \
	    echo "$(CMOC) $(CMOCOPTS) -o dhry dhry.c"; \
	    $(CMOC) $(CMOCOPTS) -o dhry dhry.c || exit 1; \
	else \
	    echo "cmoc not found: keeping the prebuilt dhry module"; test -f dhry; \
	fi

clean:
	rm -f *.o *.s *.lst *.map *.link

.PHONY: all clean
