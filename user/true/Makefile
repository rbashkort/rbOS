BUILD_DIR = build
RBOS_CC = ../rbos-cc
TARGET = $(BUILD_DIR)/true
OBJECTS = $(BUILD_DIR)/true.o
.PHONY: all clean install uninstall
all: $(TARGET)
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(RBOS_CC) -c $< -o $@
$(TARGET): $(OBJECTS) ../../sysroot/usr/lib/libc.a
	$(RBOS_CC) -dynamic $(OBJECTS) -o $@
install: $(TARGET)
	mkdir -p ../../sysroot/usr/bin
	cp $(TARGET) ../../sysroot/usr/bin/true
clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../sysroot/usr/bin/true
