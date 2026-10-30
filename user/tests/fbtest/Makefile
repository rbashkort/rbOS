BUILD_DIR = build
RBOS_CC   = ../../rbos-cc

TARGET  = $(BUILD_DIR)/fbtest
SOURCES = fbtest.c
OBJECTS = $(BUILD_DIR)/fbtest.o

.PHONY: all clean install uninstall

all: $(TARGET)

# fbtest links against pixman, a third-party library built by 'make libs'.
ifneq ($(wildcard ../../../sysroot/usr/lib/libpixman-1.so),)
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/fbtest.o: fbtest.c | $(BUILD_DIR)
	$(RBOS_CC) -I../../../sysroot/usr/include/pixman-1 -c $< -o $@

$(TARGET): $(OBJECTS)
	$(RBOS_CC) -dynamic $(OBJECTS) -lpixman-1 -o $@
	@echo "=== fbtest built with Pixman & mlibc ==="
else
$(TARGET):
	@echo "❌ Error: pixman is not installed into sysroot."
	@echo "   Run 'make libs' first (builds mlibc + ports, incl. pixman)."
	@exit 1
endif

install: $(TARGET)
	mkdir -p ../../../sysroot/usr/bin
	cp $(TARGET) ../../../sysroot/usr/bin/fbtest

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../../sysroot/usr/bin/fbtest
