BUILD_DIR = build
HOST_CC ?= gcc

TARGET = $(BUILD_DIR)/tltest

# Host static toolchain probe: `gcc -static` needs static libc on the host
# (glibc-static/musl). Minimal hosts may lack it — skip the build instead of
# breaking `make user`; rbtest reports a missing binary as SKIP, not FAIL.
HAVE_HOST_STATIC := $(shell printf 'int main(void){return 0;}\n' | $(HOST_CC) -static -x c - -o /dev/null 2>/dev/null && echo yes || echo no)

.PHONY: all clean install uninstall

ifeq ($(HAVE_HOST_STATIC),yes)
all: $(TARGET)
else
all:
	@echo "tltest: $(HOST_CC) -static unavailable, skipping (rbtest will SKIP)"
endif

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Deliberately NOT rbos-cc: this must be a host-libc static binary to test
# the kernel the way prebuilt Linux binaries see it (own PT_TLS, own startup).
$(TARGET): tltest.c | $(BUILD_DIR)
	$(HOST_CC) -static -O2 -Wall -Wextra -o $@ $<

install:
	mkdir -p ../../../sysroot/usr/bin
	if [ -f $(TARGET) ]; then cp $(TARGET) ../../../sysroot/usr/bin/tltest; \
	else echo "tltest: not built, skipping install (rbtest will SKIP)"; fi

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../../sysroot/usr/bin/tltest
