BUILD_DIR = build
RBOS_CC = ../../rbos-cc
CXX = g++
CXX_INC = -isystem ../../../sysroot/usr/include \
	-isystem ../../../mlibc/options/posix/include \
	-isystem ../../../mlibc/options/ansi/include \
	-isystem ../../../mlibc/options/bits/include \
	-isystem ../../../mlibc-sysdeps/sysdeps/rbos/include \
	-isystem $(shell g++ -print-file-name=include)
CXXFLAGS = -std=c++17 -nostdinc -D__rbos__=1 -D__RBOS_OPENBSD_COMPAT__=1 \
	$(CXX_INC)

TARGET = $(BUILD_DIR)/headertest
OBJECTS = $(BUILD_DIR)/test.o

.PHONY: all clean install uninstall

all: $(TARGET) $(BUILD_DIR)/compat.o $(BUILD_DIR)/compat-cxx.o

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Ordinary view: no OpenBSD names, arc4random via _DEFAULT_SOURCE.
$(BUILD_DIR)/test.o: test.c | $(BUILD_DIR)
	$(RBOS_CC) -Werror=implicit-function-declaration -c $< -o $@

# Opt-in view: pledge/unveil/arc4random must come from the standard headers.
$(BUILD_DIR)/compat.o: compat.c | $(BUILD_DIR)
	$(RBOS_CC) -D__RBOS_OPENBSD_COMPAT__=1 \
		-Werror=implicit-function-declaration -c $< -o $@

$(BUILD_DIR)/compat-cxx.o: compat.cc | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS) ../../../sysroot/usr/lib/libc.a
	$(RBOS_CC) -dynamic $(OBJECTS) -o $@

install: $(TARGET)
	mkdir -p ../../../sysroot/usr/bin
	cp $(TARGET) ../../../sysroot/usr/bin/headertest

clean:
	rm -rf $(BUILD_DIR)

uninstall:
	rm -f ../../../sysroot/usr/bin/headertest
