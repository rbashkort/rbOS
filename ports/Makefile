# rbOS ports: third-party userspace + toolchain.
#
# Sources are NOT vendored in this repo:
#   - pixman, oksh, sbase, tcc are git submodules (see ../.gitmodules).
#     First checkout: `git submodule update --init` (or `make fetch`).
#   - binutils (gnu/) and e2fsprogs (e2fsck/) are release tarballs fetched
#     by `make fetch` (URL + sha256 pinned in gnu/Makefile and e2fsck/).
# Only wrappers, patches, build configs and fetch pins live in this repo.
#
# pixman / oksh / sbase use their own in-tree Makefiles (all/install/clean).
# tcc   uses a saved rbOS config (tcc-config/) + upstream Makefile.
# gnu   uses ports/gnu/Makefile (binutils: fetch, build via ./build, install).
#
# OpenBSD-compatible declarations (pledge in <unistd.h>, arc4random* in
# <stdlib.h>) are opt-in.  Exporting this makes ports/rbos-cc add
# -D__RBOS_OPENBSD_COMPAT__; it does not affect ordinary rbOS programs.
export RBOS_OPENBSD_COMPAT = 1

SYSROOT := $(abspath ../sysroot)

PORTS     = pixman oksh sbase
RBOS_PORTS = tcc gnu e2fsck

.PHONY: all clean install uninstall fetch $(PORTS) $(RBOS_PORTS) \
        $(addprefix build-,$(PORTS) $(RBOS_PORTS)) \
        $(addprefix install-,$(PORTS) $(RBOS_PORTS))

all: $(addprefix build-,$(PORTS) $(RBOS_PORTS))

install: $(addprefix install-,$(PORTS) $(RBOS_PORTS))

# First checkout (fresh clone): submodules + tarballs.
fetch:
	@echo "=== Fetching port sources ==="
	git -C $(abspath ..) submodule update --init -- ports/pixman ports/oksh ports/sbase ports/tcc
	$(MAKE) -C gnu fetch
	$(MAKE) -C e2fsck fetch

clean:
	@echo "=== Cleaning port: pixman ==="
	rm -rf pixman/build-rbos
	@echo "=== Cleaning port: oksh ==="
	$(MAKE) -C oksh clean
	@echo "=== Cleaning port: sbase ==="
	$(MAKE) -C sbase clean
	@echo "=== Cleaning port: tcc ==="
	$(MAKE) -C tcc clean
	@echo "=== Cleaning port: gnu ==="
	$(MAKE) -C gnu clean
	@echo "=== Cleaning port: e2fsck ==="
	$(MAKE) -C e2fsck clean

# --- pixman (meson, out-of-tree build) ---
# Upstream has no Makefile; the wrapper recipe lives here.
# Cross file: pixman-cross.txt. Build dir: pixman/build-rbos (ignored).
PIXMAN_BUILD := pixman/build-rbos
MLIBC_LIB_DYN := $(realpath ../mlibc-sysdeps/sysdeps/rbos/lib-dynamic)

build-pixman:
	@if [ ! -d "$(PIXMAN_BUILD)" ]; then \
		echo "=== Configuring pixman (meson) ==="; \
		meson setup $(PIXMAN_BUILD) pixman \
			--cross-file pixman-cross.txt \
			--prefix=/usr \
			-Dc_args="-mcmodel=small -fPIC -D_GNU_SOURCE --sysroot=$(SYSROOT) -I$(SYSROOT)/usr/include" \
			-Dc_link_args="--sysroot=$(SYSROOT) -L$(MLIBC_LIB_DYN) -L$(SYSROOT)/usr/lib -lc" \
			-Dtests=disabled \
			-Ddemos=disabled \
			-Dgtk=disabled \
			-Dopenmp=disabled; \
	fi
	ninja -C $(PIXMAN_BUILD)

install-pixman: build-pixman
	@echo "=== Installing pixman into sysroot ==="
	DESTDIR=$(SYSROOT) ninja -C $(PIXMAN_BUILD) install

uninstall-pixman:
	DESTDIR=$(SYSROOT) ninja -C $(PIXMAN_BUILD) uninstall || true

# --- oksh (ibara/oksh @ oksh-7.9 + rbOS nocurses fallback) ---
# configure output is snapshotted in oksh-config/ (re-running configure
# against mlibc detects different features and the generated Makefile was
# hand-trimmed for the port: bundled-func OBJS removed, -lcurses dropped,
# explicit libc.a dep). Do NOT re-run configure; copy the snapshot instead.
build-oksh:
	@if ! grep -q "cur_term" oksh/sh.h 2>/dev/null; then \
		echo "=== oksh: applying rbOS patches ==="; \
		for p in patches/oksh/*.patch; do \
			patch -d oksh -p1 --forward --batch < $$p; \
		done; \
	fi
	@if [ ! -f oksh/pconfig.h ]; then \
		echo "=== oksh: installing rbOS build configs ==="; \
		cp oksh-config/Makefile oksh-config/pconfig.h oksh/; \
	fi
	$(MAKE) -C oksh all

install-oksh: build-oksh
	@echo "=== Installing oksh into sysroot ==="
	$(MAKE) -C oksh install

uninstall-oksh:
	$(MAKE) -C oksh uninstall

# --- sbase (suckless, CLI overrides only; upstream config.mk untouched) ---
SBASE_VARS := CC=../rbos-cc PREFIX=../../sysroot/usr MANPREFIX=../../sysroot/usr/doc/man
SBASE_VARS += CFLAGS="-Os -g0 -Wall" LDFLAGS=-dynamic

build-sbase:
	@echo "=== Building port: sbase ==="
	$(MAKE) -C sbase $(SBASE_VARS) all

install-sbase: build-sbase
	@echo "=== Installing sbase into sysroot ==="
	$(MAKE) -C sbase $(SBASE_VARS) install

uninstall-sbase:
	$(MAKE) -C sbase $(SBASE_VARS) uninstall

# --- tcc (TinyCC @ mob HEAD + saved rbOS build configs) ---
# The rbos-target tcc cannot run on the host, so libtcc1.a is built with
# rbos-cc directly (upstream cross mode: <target>-libtcc1-usegcc=yes).
# configure is NOT re-run: tcc-config/ holds the proven-good generated files.
# NOTE: CC is passed as an absolute path — config.mak uses a relative
# ../rbos-cc which breaks in the lib/ sub-make (resolves to tcc/rbos-cc).
# Only tcc + libtcc1.a are built (docs need configure-generated config.texi
# and are not shipped — same install set as the previous prebuilt setup).
# LIBS is emptied: upstream unconditionally appends -lm, which rbos-cc
# would resolve to the HOST glibc libm.so.6 (no --sysroot on link lines and
# no libm in the sysroot) — poisoning the binary with an unsatisfiable
# DT_NEEDED. tcc references no math symbols, so -lm is pure baggage.
# patches/tcc/ are applied first (sentinel: GCC-int predefs for mlibc).
build-tcc:
	@if ! grep -q "__INT_LEAST8_TYPE__" tcc/tccpp.c 2>/dev/null; then \
		echo "=== tcc: applying rbOS patches ==="; \
		for p in patches/tcc/*.patch; do \
			patch -d tcc -p1 --forward --batch < $$p; \
		done; \
	fi
	@if [ ! -f tcc/config.h ]; then \
		echo "=== tcc: installing rbOS build configs ==="; \
		cp tcc-config/config.h tcc-config/config.mak tcc/; \
	fi
	@echo "=== Building port: tcc ==="
	$(MAKE) -C tcc CC=$(abspath rbos-cc) LIBS= x86_64-libtcc1-usegcc=yes tcc libtcc1.a

# NOTE: libtcc1.a lands in tcc/lib/ (upstream quirk: config.mak TOP=. is
# inherited by the lib/ sub-make, so $(TOP)/libtcc1.a resolves to ./lib/).
# Install from the real location instead of fighting upstream paths.
install-tcc: build-tcc
	@echo "=== Installing tcc into sysroot ==="
	mkdir -p $(SYSROOT)/usr/bin $(SYSROOT)/usr/lib/tcc/include $(SYSROOT)/usr/include
	cp -f tcc/tcc $(SYSROOT)/usr/bin/tcc
	chmod 755 $(SYSROOT)/usr/bin/tcc
	cp -f tcc/lib/libtcc1.a $(SYSROOT)/usr/lib/tcc/libtcc1.a
	cp -f tcc/lib/libtcc1.a $(SYSROOT)/usr/lib/libtcc1.a
	cp -f tcc/include/*.h $(SYSROOT)/usr/lib/tcc/include/
	cp -f tcc/include/tccdefs.h $(SYSROOT)/usr/include/tccdefs.h

uninstall-tcc:
	rm -f $(SYSROOT)/usr/bin/tcc
	rm -f $(SYSROOT)/usr/lib/tcc/libtcc1.a $(SYSROOT)/usr/lib/libtcc1.a
	rm -f $(SYSROOT)/usr/lib/tcc/include/*.h
	rm -f $(SYSROOT)/usr/include/tccdefs.h

# --- gnu binutils: build via ./build, install into sysroot ---
build-gnu:
	$(MAKE) -C gnu all

install-gnu:
	$(MAKE) -C gnu install

uninstall-gnu:
	$(MAKE) -C gnu uninstall

# --- e2fsck: static checker via ./build, install into sysroot ---
build-e2fsck:
	$(MAKE) -C e2fsck all

install-e2fsck:
	$(MAKE) -C e2fsck install

uninstall-e2fsck:
	$(MAKE) -C e2fsck uninstall

uninstall:
	@echo "=== Uninstalling ports from sysroot ==="
	$(MAKE) uninstall-pixman
	$(MAKE) uninstall-oksh
	$(MAKE) uninstall-sbase
	$(MAKE) uninstall-tcc
	$(MAKE) uninstall-gnu
	$(MAKE) uninstall-e2fsck
