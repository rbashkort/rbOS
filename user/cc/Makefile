# Guest C driver (tcc-based). Script, nothing to build — just stage it into
# the sysroot so every image has /usr/bin/cc (previously it lived only in
# a home directory and was wiped by `make distclean`).
.PHONY: all clean install uninstall

all:
	@echo "cc: script, nothing to build"

install:
	mkdir -p ../../sysroot/usr/bin
	cp cc ../../sysroot/usr/bin/cc
	chmod +x ../../sysroot/usr/bin/cc

clean:

uninstall:
	rm -f ../../sysroot/usr/bin/cc
