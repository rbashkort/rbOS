# Host-built Linux/glibc compatibility probes
#
# These are NOT part of the rbOS userspace build. They are compiled with the
# *host* toolchain against the *host* glibc, statically linked, and installed
# under `sysroot/home/hosttests/`. Running them under rbOS is the compatibility
# oracle from plan.md: a host-built Linux binary must start and behave as it
# would on Linux, regardless of what mlibc happens to call.
#
# Static glibc binaries are big (0.6-2.5 MiB each), so they are opt-in:
#
#   make host-tests              # build + install into sysroot/home/hosttests
#   make host-tests-remove       # remove them from sysroot again
#   make -C host-tests           # build only
#   make -C host-tests clean     # remove build/
#
# Default `make test`/`make filesystem` does not install them; install them
# explicitly when you want to run the probes, then remove them before shrink-
# ing/archiving the image.
#
# `STATIC=0` builds dynamically linked probes for future use (rbOS currently
# has no glibc dynamic loader, so those need the host ld.so/libs copied in
# manually and are not expected to run yet).

BUILD_DIR ?= build
DESTDIR   ?= ../sysroot/home/hosttests
HOST_CC   ?= cc
HOST_CXX  ?= c++
CFLAGS    ?= -O2 -Wall -Wextra
CXXFLAGS  ?= $(CFLAGS)
STATIC    ?= 1
STRIP     ?= -s

ifeq ($(STATIC),1)
LINK_MODE := -static
else
LINK_MODE :=
endif

C_TESTS   := hello pthread tls
CXX_TESTS := cxx
HOST_BINS := $(C_TESTS) $(CXX_TESTS)
BINS      := $(addprefix $(BUILD_DIR)/,$(HOST_BINS))

.PHONY: all install uninstall clean

all: $(BINS)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%: %.c | $(BUILD_DIR)
	$(HOST_CC) $(CFLAGS) $(LINK_MODE) $(STRIP) -o $@ $<

$(BUILD_DIR)/%: %.cpp | $(BUILD_DIR)
	$(HOST_CXX) $(CXXFLAGS) $(LINK_MODE) $(STRIP) -o $@ $<

install: all
	mkdir -p $(DESTDIR)
	cp $(BINS) $(DESTDIR)/
	@echo "Installed host Linux probes into $(DESTDIR)/"
	@echo "Remove with: make host-tests-remove"

uninstall:
	@set -e; \
	dest="$$(realpath -m -- "$(DESTDIR)")"; \
	expected="$$(realpath -m -- "../sysroot/home/hosttests")"; \
	if [ "$$dest" != "$$expected" ]; then \
		echo "host-tests: refusing unexpected DESTDIR: $$dest (expected $$expected)" >&2; \
		exit 1; \
	fi; \
	if [ -L "$(DESTDIR)" ]; then \
		echo "host-tests: refusing symlinked DESTDIR: $(DESTDIR)" >&2; \
		exit 1; \
	fi; \
	if [ ! -d "$(DESTDIR)" ]; then \
		echo "host-tests: $(DESTDIR) is already absent"; \
		exit 0; \
	fi; \
	removed=0; \
	for b in $(HOST_BINS); do \
		p="$(DESTDIR)/$$b"; \
		if [ -L "$$p" ]; then \
			echo "host-tests: refusing symlink: $$p" >&2; \
			exit 1; \
		fi; \
		if [ -e "$$p" ] && [ ! -f "$$p" ]; then \
			echo "host-tests: refusing non-regular file: $$p" >&2; \
			exit 1; \
		fi; \
		if [ -f "$$p" ]; then \
			magic="$$(head -c 4 -- "$$p" 2>/dev/null | od -An -tx1 | tr -d ' \n')"; \
			if [ "$$magic" != "7f454c46" ]; then \
				echo "host-tests: refusing non-ELF file: $$p" >&2; \
				exit 1; \
			fi; \
			rm -f -- "$$p"; \
			echo "host-tests: removed $$p"; \
			removed=$$((removed + 1)); \
		fi; \
	done; \
	if ! rmdir -- "$(DESTDIR)" 2>/dev/null; then \
		echo "host-tests: left $(DESTDIR)/ in place (not empty; removed $$removed known probe(s))" >&2; \
		exit 1; \
	fi; \
	echo "host-tests: removed $(DESTDIR)/"

clean:
	rm -rf $(BUILD_DIR)
