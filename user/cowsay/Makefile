BUILD_DIR = build
RBOS_CC = ../rbos-cc

TARGET = $(BUILD_DIR)/cowsay
SOURCES = cowsay.c
OBJECTS = $(BUILD_DIR)/cowsay.o

.PHONY: all clean install uninstall

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/cowsay.o: cowsay.c | $(BUILD_DIR)
	$(RBOS_CC) -c $< -o $@

$(TARGET): $(OBJECTS) ../../sysroot/usr/lib/libc.a
	$(RBOS_CC) -dynamic $(OBJECTS) -o $@
	@echo "=== cowsay built with mlibc ==="

dynamic: $(OBJECTS)
	$(RBOS_CC) -dynamic $(OBJECTS) -o $(TARGET)

install: $(TARGET)
	mkdir -p ../../sysroot/usr/bin
	cp $(TARGET) ../../sysroot/usr/bin/cowsay
	@echo "[OK] Installed cowsay to sysroot/usr/bin/"

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../sysroot/usr/bin/cowsay
