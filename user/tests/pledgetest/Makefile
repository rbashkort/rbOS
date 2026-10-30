BUILD_DIR = build
RBOS_CC = ../../rbos-cc

TARGET = $(BUILD_DIR)/pledgetest
HELPER = $(BUILD_DIR)/pledge-helper
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

# No-libc exec target: only open(2) and exit(2), so the pledge mask exactly
# covers what the helper needs. A dynamic helper would exercise the loader's
# own path accesses and obscure the exec-inheritance test.
$(HELPER): pledge-helper.s | $(BUILD_DIR)
	gcc -nostdlib -static -o $@ $<

install: $(TARGET) $(HELPER)
	mkdir -p ../../../sysroot/usr/bin
	cp $(TARGET) ../../../sysroot/usr/bin/pledgetest
	cp $(HELPER) ../../../sysroot/usr/bin/pledge-helper

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../../sysroot/usr/bin/pledge-helper
	rm -f ../../../sysroot/usr/bin/pledgetest
