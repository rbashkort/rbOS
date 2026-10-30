BUILD_DIR = build
RBOS_CC = ../../rbos-cc

TARGET = $(BUILD_DIR)/unveiltest
HELPER = $(BUILD_DIR)/unveil-helper
OBJECTS = $(BUILD_DIR)/test.o

.PHONY: all clean install uninstall

all: $(TARGET) $(HELPER)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test.o: test.c | $(BUILD_DIR)
	$(RBOS_CC) -D__RBOS_OPENBSD_COMPAT__=1 \
		-Werror=implicit-function-declaration -c $< -o $@

$(TARGET): $(OBJECTS) ../../../sysroot/usr/lib/libc.a
	$(RBOS_CC) -dynamic $(OBJECTS) -o $@

# Static helper: exec must not need a loader path that the test did not
# unveil, and the helper's only path syscalls are the two under test.
$(HELPER): unveil-helper.c | $(BUILD_DIR)
	$(RBOS_CC) -D__RBOS_OPENBSD_COMPAT__=1 $< -o $@

install: $(TARGET) $(HELPER)
	mkdir -p ../../../sysroot/usr/bin
	cp $(TARGET) ../../../sysroot/usr/bin/unveiltest
	cp $(HELPER) ../../../sysroot/usr/bin/unveil-helper

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../../sysroot/usr/bin/unveil-helper
	rm -f ../../../sysroot/usr/bin/unveiltest
