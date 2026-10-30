BUILD_DIR = build
RBOS_CC   = ../rbos-cc

TARGET  = $(BUILD_DIR)/rbwm
SOURCES = rbwm.c
OBJECTS = $(BUILD_DIR)/rbwm.o

.PHONY: all clean install uninstall

all: $(TARGET)

# rbwm links against pixman, a third-party library built by 'make libs'.
ifneq ($(wildcard ../../sysroot/usr/lib/libpixman-1.so),)
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/rbwm.o: rbwm.c | $(BUILD_DIR)
	$(RBOS_CC) -I../../sysroot/usr/include/pixman-1 -c $< -o $@

$(TARGET): $(OBJECTS)
	$(RBOS_CC) -dynamic $(OBJECTS) -lpixman-1 -o $@
	@echo "=== rbwm built with Pixman & mlibc ==="
else
$(TARGET):
	@echo "❌ Error: pixman is not installed into sysroot."
	@echo "   Run 'make libs' first (builds mlibc + ports, incl. pixman)."
	@exit 1
endif

install: $(TARGET)
	mkdir -p ../../sysroot/usr/bin
	cp $(TARGET) ../../sysroot/usr/bin/rbwm

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../sysroot/usr/bin/rbwm
