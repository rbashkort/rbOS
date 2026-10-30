CC = ../rbos-cc
LDFLAGS ?= -dynamic

TARGET = rvi
BUILD_DIR = build
SYNTAX = 1

SOURCES = rvi.c commands.c

ifeq ($(SYNTAX),1)
SOURCES += syntax.c
CPPFLAGS += -DRVI_ENABLE_SYNTAX=1
else
CPPFLAGS += -DRVI_ENABLE_SYNTAX=0
endif

OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean run install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	./$(TARGET) test.txt

install: $(TARGET)
	mkdir -p ../../sysroot/usr/bin
	cp $(TARGET) ../../sysroot/usr/bin/rvi

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPS)

uninstall:
	rm -f ../../sysroot/usr/bin/rvi
