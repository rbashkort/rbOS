BUILD_DIR = build
RBOS_CC = ../../rbos-cc

TARGET = $(BUILD_DIR)/robusttest
SOURCES = robusttest.c
OBJECTS = $(BUILD_DIR)/robusttest.o

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS) ../../../sysroot/usr/lib/libc.a
	$(RBOS_CC) -dynamic $(OBJECTS) -o $@
	@echo "=== robusttest built with mlibc ==="

$(BUILD_DIR)/robusttest.o: robusttest.c | $(BUILD_DIR)
	$(RBOS_CC) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

install: $(TARGET)
	mkdir -p ../../../sysroot/usr/bin
	cp $(TARGET) ../../../sysroot/usr/bin/robusttest

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../../sysroot/usr/bin/robusttest
