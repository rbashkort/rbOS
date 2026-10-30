BUILD_DIR = build
RBOS_CC = ../../rbos-cc
TARGET = $(BUILD_DIR)/stacktest
GEO = $(BUILD_DIR)/stackgeo
OBJECTS = $(BUILD_DIR)/stacktest.o

.PHONY: all clean install uninstall

all: $(TARGET) $(GEO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# -Wno-infinite-recursion: rec_touch/rec_fill_then_walk are depth-bounded by
# their target arg; the warning is noise (and the depth self-checks inside
# fail closed if the compiler ever folds the frames).
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(RBOS_CC) -Wno-infinite-recursion -c $< -o $@

# Deliberately STATIC (no -dynamic): deep-recursion/overflow tests must run
# on the kernel stack layout directly. (The gap-geometry check lives in the
# no-libc stackgeo helper: any libc startup mmaps its own TLS and would hide
# the kernel-placed TLS/TCB this test measures.)
$(TARGET): $(OBJECTS) ../../../sysroot/usr/lib/libc.a
	$(RBOS_CC) $(OBJECTS) -o $@

# No-libc entry probe (pure _start + raw syscalls): at entry FS is still the
# kernel-placed dummy TCB below the stack reserve.
$(GEO): stackgeo.s | $(BUILD_DIR)
	gcc -nostdlib -static -o $@ $<

install: $(TARGET) $(GEO)
	mkdir -p ../../../sysroot/usr/bin
	cp $(TARGET) ../../../sysroot/usr/bin/stacktest
	cp $(GEO) ../../../sysroot/usr/bin/stackgeo

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../../sysroot/usr/bin/stackgeo
	rm -f ../../../sysroot/usr/bin/stacktest
