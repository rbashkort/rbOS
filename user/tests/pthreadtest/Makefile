BUILD_DIR = build
RBOS_CC = ../../rbos-cc

TARGET = $(BUILD_DIR)/pthreadtest
OBJECTS = $(BUILD_DIR)/test.o

.PHONY: all clean install uninstall

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test.o: test.c | $(BUILD_DIR)
	$(RBOS_CC) -c $< -o $@

$(TARGET): $(OBJECTS) ../../../sysroot/usr/lib/libc.a
	$(RBOS_CC) -dynamic $(OBJECTS) -o $@ -lpthread

install: $(TARGET)
	mkdir -p ../../../sysroot/usr/bin
	cp $(TARGET) ../../../sysroot/usr/bin/pthreadtest

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../../sysroot/usr/bin/pthreadtest
