BUILD_DIR = build
RBOS_CC = ../../rbos-cc

TARGET = $(BUILD_DIR)/vttest
SOURCES = vttest.c
OBJECTS = $(BUILD_DIR)/vttest.o

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS) ../../../sysroot/usr/lib/libc.a
	$(RBOS_CC) -dynamic $(OBJECTS) -o $@
	@echo "=== vttest built with mlibc ==="

$(BUILD_DIR)/vttest.o: vttest.c | $(BUILD_DIR)
	$(RBOS_CC) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

install: $(TARGET)
	mkdir -p ../../../sysroot/usr/bin
	cp $(TARGET) ../../../sysroot/usr/bin/vttest

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../../sysroot/usr/bin/vttest
