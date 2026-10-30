# rbOS Makefile

# Directories
KERNEL_DIR = kernel
BUILD_DIR  = build
ISO_DIR    = iso_root
SYSROOT    = sysroot
USER_DIR   = user

# NO_TESTS=1 builds/installs a deployment sysroot without the autotest
# binaries. `make test` keeps the default (tests included).
NO_TESTS ?= 0

# Output (Rust kernel built by cargo, see kernel/.cargo/config.toml for target & linker script)
KERNEL = $(KERNEL_DIR)/target/x86_64-unknown-none/release/kernel

.PHONY: all clean distclean iso test-iso test test-smp run run-no-disk run-nographic run-debug \
        mlibc mlibc-dynamic ports libs user populate-sysroot sysroot-layout \
        strip-sysroot host-tests host-tests-remove tests-remove image \
        filesystem uninstall tools world help

all: $(KERNEL)

# --- Kernel Build (Rust/cargo) ---

CARGO = cargo

$(KERNEL): $(shell find $(KERNEL_DIR)/src -name '*.rs') $(KERNEL_DIR)/Cargo.toml $(KERNEL_DIR)/.cargo/config.toml $(KERNEL_DIR)/linker.ld
	cd $(KERNEL_DIR) && $(CARGO) build --release

# --- Third-party components (build once, rarely rebuilt) ---

mlibc:
	@echo "=== Building mlibc for rbOS ==="
	./tools/build-mlibc-static.sh
	@echo "✅ mlibc built successfully"

mlibc-dynamic:
	@echo "=== Building dynamic mlibc (rtld) for rbOS ==="
	./tools/build-mlibc-dynamic.sh
	@echo "✅ dynamic mlibc built successfully"

ports:
	@echo "=== Building & installing ports (oksh, pixman, ...) ==="
	$(MAKE) -C ports all
	$(MAKE) -C ports install
	@echo "✅ Ports built successfully"

# Everything third-party in one shot.
# Order matters: populate-sysroot must refresh sysroot/usr/lib/libc.a
# BEFORE ports build, since ports depend on that copy for relinking.
# The final strip keeps the deployment image small; build trees keep debug.
libs: mlibc mlibc-dynamic populate-sysroot ports strip-sysroot

# Host-built Linux/glibc probes (opt-in, big static binaries).
# Sources + Makefile: host-tests/. Installed under sysroot/home/hosttests.
host-tests:
	$(MAKE) -C host-tests install

host-tests-remove:
	$(MAKE) -C host-tests uninstall

# Remove the autotest binaries from the deployment sysroot, plus the opt-in
# host probes. The script uses an explicit allowlist and refuses to delete
# anything that does not look like an rbOS test executable; /usr/bin/test and
# /usr/bin/[ are POSIX utilities and are preserved. 
# Use DRY_RUN=1 to only print what would be removed.
#
# Restore tests: make -C user/tests install && make strip-sysroot
# Rebuild a test-free image: make image
tests-remove:
	@./tools/remove-tests.sh $(if $(filter 1,$(DRY_RUN)),--dry-run)
	@if [ "$(DRY_RUN)" = "1" ]; then \
		echo "tests-remove: DRY_RUN=1, host probes left in place"; \
	else \
		$(MAKE) host-tests-remove; \
	fi
	@if [ "$(DRY_RUN)" = "1" ]; then \
		echo "tests-remove: dry run complete (nothing removed)"; \
	else \
		echo "tests-remove: deployment sysroot is test-free."; \
	fi
	@echo "  Restore tests: make sysroot-layout (or: make -C user/tests install && make strip-sysroot)"
	@echo "  Rebuild image from the test-free sysroot: make image"

strip-sysroot:
	./tools/strip-sysroot.sh

# Remove installed userspace + ports from the sysroot (keeps build trees).
# Restore with `make user && make -C ports install` (or `make world`).
uninstall:
	$(MAKE) -C $(USER_DIR) uninstall
	$(MAKE) -C ports uninstall

# --- userspace ---

user:
	@echo "=== Building userspace tools ==="
	@if [ ! -f sysroot/usr/lib/libc.a ]; then \
		echo "❌ Error: libc not installed into sysroot."; \
		echo "   Run 'make libs' first (builds mlibc + ports)."; \
		exit 1; \
	fi
	$(MAKE) -C $(USER_DIR) NO_TESTS=$(NO_TESTS)
	@echo "✅ Userspace built successfully"

populate-sysroot:
	@if [ ! -f mlibc-sysdeps/sysdeps/rbos/lib/libc.a ]; then \
		echo "❌ Error: mlibc is not built yet."; \
		echo "   Run 'make libs' first (builds mlibc + ports)."; \
		exit 1; \
	fi
	@echo "=== Populating sysroot with mlibc ==="
	./tools/populate-sysroot.sh

# --- Sysroot & Ext2 Image ---
#
# Assembles the image from already-built/installed components; third-party
# libraries are NOT rebuilt here. Use 'make libs' to refresh them.

sysroot-layout: populate-sysroot user
	@echo "=== Setting up UsrMerge Layout ==="
	@mkdir -p $(SYSROOT)/usr/bin $(SYSROOT)/usr/lib $(SYSROOT)/sbin
	@mkdir -p $(SYSROOT)/etc $(SYSROOT)/home $(SYSROOT)/var $(SYSROOT)/tmp
	@printf '%s\n' 'NAME="rbOS"' 'PRETTY_NAME="rbOS 0.1"' \
		'VERSION="0.1"' 'VERSION_ID="0.1"' 'ID=rbos' 'ID_LIKE=linux' \
		> $(SYSROOT)/etc/os-release
	@printf '%s\n' '/bin/sh' '/usr/bin/oksh' > $(SYSROOT)/etc/shells
	@ln -sf usr/bin $(SYSROOT)/bin
	@ln -sf usr/lib $(SYSROOT)/lib
	@ln -sf usr/lib $(SYSROOT)/lib64
	@echo "=== Installing user binaries (NO_TESTS=$(NO_TESTS)) ==="
	$(MAKE) -C $(USER_DIR) install NO_TESTS=$(NO_TESTS)
	@ln -sf /usr/bin/rbit $(SYSROOT)/sbin/init
	# Default shell: /bin/sh -> oksh (installed by 'make libs').
	@ln -sf oksh $(SYSROOT)/usr/bin/sh
	@echo "=== Creating test files ==="
	@echo "Hello from rbOS!" > $(SYSROOT)/home/test.txt
	@if [ ! -e $(SYSROOT)/usr/bin/oksh ]; then \
		echo "❌ Error: /usr/bin/oksh missing from sysroot."; \
		echo "   Third-party components are not installed — run 'make libs'."; \
		exit 1; \
	fi
	@if [ "$(NO_TESTS)" = "1" ]; then \
		echo "=== Removing test binaries (NO_TESTS=1) ==="; \
		./tools/remove-tests.sh; \
		$(MAKE) host-tests-remove; \
	fi
	@echo "=== Stripping sysroot (deployment copy only) ==="
	./tools/strip-sysroot.sh

# Recreate the image from the sysroot as it is now, without rebuilding or
# reinstalling anything. Useful after `make tests-remove` (which must not be
# followed by `make filesystem`, because that would reinstall the tests).
image:
	@if [ ! -d $(SYSROOT) ]; then \
		echo "❌ Error: $(SYSROOT) does not exist."; \
		echo "   Run 'make filesystem' first."; \
		exit 1; \
	fi
	@echo "=== Generating Ext2 filesystem image ==="
	@rm -f filesystem.img
	mke2fs -t ext2 -d $(SYSROOT) -r 1 -N 0 -O ^64bit -m 0 filesystem.img 512M
	@echo "✅ Filesystem image created: filesystem.img"

filesystem: sysroot-layout
	@$(MAKE) image

tools: filesystem

# --- Bootable HDD image (DOS/MBR + FAT32 boot + ext2 root, Limine BIOS) ---
#
# The disk image is self-contained: an MBR partition table, a FAT32 boot
# partition holding /boot (Limine only reads FAT/ISO9660 — ext2 support was
# dropped upstream), and an ext2 partition with the sysroot. The kernel
# finds the rootfs by probing LBA0 for a DOS/MBR table and using the first
# 0x83 partition (falls back to LBA0 for raw images like filesystem.img).
# CMDLINE works like for `iso`; `make disk-test` boots the rbtest suite
# from disk with the same host-side watchdog as `make test`.
# UEFI/GPT+ESP is future work (needs OVMF to validate here).
DISK_IMG ?= disk.img
DISK_ROOTFS := build/disk-rootfs.ext2
DISK_FAT := build/disk-fat.img
DISK_STAGE := build/disk-stage
LIMINE_VERSION := 12.4.1
LIMINE_TARBALL := build/limine-binary.tar.xz
LIMINE_URL := https://github.com/limine-bootloader/limine/releases/download/v$(LIMINE_VERSION)/limine-binary.tar.xz
LIMINE_DEPLOY := build/limine-binary/limine

$(LIMINE_DEPLOY):
	@echo "=== Fetching limine deploy tool v$(LIMINE_VERSION) ==="
	@mkdir -p build
	@if [ ! -f "$(LIMINE_TARBALL)" ]; then \
		curl -fL -o $(LIMINE_TARBALL) $(LIMINE_URL); \
	fi
	rm -rf build/limine-binary
	mkdir -p build/limine-binary
	tar -xf $(LIMINE_TARBALL) -C build/limine-binary --strip-components=1
	@echo "=== Building limine deploy tool (host cc) ==="
	$(MAKE) -C build/limine-binary limine
	@echo "limine deploy tool ready"

disk: $(KERNEL) $(LIMINE_DEPLOY)
	@if [ ! -d $(SYSROOT) ]; then \
		echo "❌ Error: $(SYSROOT) does not exist."; \
		echo "   Run 'make filesystem' first."; \
		exit 1; \
	fi
	@echo "=== Staging disk rootfs (sysroot) ==="
	@rm -rf $(DISK_STAGE)
	@mkdir -p $(DISK_STAGE)
	cp -a $(SYSROOT)/. $(DISK_STAGE)/
	@echo "=== Creating ext2 rootfs ==="
	@rm -f $(DISK_ROOTFS)
	mke2fs -t ext2 -d $(DISK_STAGE) -r 1 -N 0 -O ^64bit -m 0 $(DISK_ROOTFS) 512M
	@echo "=== Creating FAT32 boot partition (mtools, no root needed) ==="
	@command -v mcopy >/dev/null || (echo "disk: mtools not found (see README Requirements)" >&2; exit 1)
	@rm -f $(DISK_FAT)
	truncate -s 64M $(DISK_FAT)
	mkfs.fat -F 32 -n RBOSBOOT $(DISK_FAT) > /dev/null
	mmd -i $(DISK_FAT) ::/boot
	mmd -i $(DISK_FAT) ::/boot/limine
	mcopy -i $(DISK_FAT) $(KERNEL) ::/boot/kernel.elf
	mcopy -i $(DISK_FAT) limine/bin/limine-bios.sys ::/boot/limine/limine-bios.sys
	@printf 'timeout: 0\n\n/rbOS\n    protocol: limine\n    kernel_path: boot():/boot/kernel.elf\n    cmdline: $(CMDLINE)\n' > build/disk-limine.conf
	mcopy -i $(DISK_FAT) build/disk-limine.conf ::/boot/limine/limine.conf
	@echo "=== Partitioning $(DISK_IMG) (DOS/MBR: FAT32 boot + ext2 root) ==="
	@rm -f $(DISK_IMG)
	truncate -s 606M $(DISK_IMG)
	printf 'label: dos\nstart=2048, size=131072, type=c, bootable\nstart=133120, size=1048576, type=83\n' | sfdisk --quiet $(DISK_IMG)
	@echo "=== Installing partitions + Limine BIOS ==="
	dd if=$(DISK_FAT) of=$(DISK_IMG) bs=512 seek=2048 conv=notrunc status=none
	dd if=$(DISK_ROOTFS) of=$(DISK_IMG) bs=512 seek=133120 conv=notrunc status=none
	$(LIMINE_DEPLOY) bios-install $(DISK_IMG)
	@echo "✅ Disk image created: $(DISK_IMG) (cmdline: $(CMDLINE))"

# Headless disk test: disk image that boots rbtest on serial, same watchdog.
disk-test: $(KERNEL)
	@$(MAKE) disk CMDLINE="$(TEST_CMDLINE)" DISK_IMG="$(DISK_IMG)"
	chmod +x tools/rbtest-host.sh
	./tools/rbtest-host.sh --disk $(DISK_IMG) --smp $(SMP) --timeout $(TEST_TIMEOUT) --stall $(TEST_STALL)

# --- Bootable ISO ---
#
# CMDLINE overrides the kernel command line baked into the ISO.
# Default matches boot/limine.conf. The autotest ISO uses
# `make test-iso` (CMDLINE=console=ttyS0 rbtest: PID1 becomes /usr/bin/rbtest).
CMDLINE ?= console=tty1
TEST_CMDLINE ?= console=ttyS0 rbtest
TEST_ISO ?= rbos-test.iso
ISO_OUT ?= rbos.iso

iso: $(KERNEL)
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/limine $(ISO_DIR)/EFI/BOOT
	@cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	@printf 'timeout: 0\n\n/rbOS\n    protocol: limine\n    kernel_path: boot():/boot/kernel.elf\n    cmdline: $(CMDLINE)\n' > $(ISO_DIR)/boot/limine/limine.conf
	@cp $(ISO_DIR)/boot/limine/limine.conf $(ISO_DIR)/limine.conf
	@cp $(ISO_DIR)/boot/limine/limine.conf $(ISO_DIR)/EFI/BOOT/limine.conf
	@cp limine/bin/limine-bios.sys $(ISO_DIR)/boot/limine/
	@cp limine/bin/limine-bios-cd.bin $(ISO_DIR)/boot/limine/
	@cp limine/bin/limine-uefi-cd.bin $(ISO_DIR)/boot/limine/
	@cp limine/bin/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO_OUT) 2>&1 | grep -v "NOTE : Not all filenames comply with standard ISO 9660" || true
	@echo "✅ ISO created successfully: $(ISO_OUT) (cmdline: $(CMDLINE))"

# Autotest ISO: same kernel, but PID1 is the non-interactive test runner.
test-iso: $(KERNEL)
	@$(MAKE) iso CMDLINE="$(TEST_CMDLINE)" ISO_OUT="$(TEST_ISO)"
	@echo "✅ Test ISO created: $(TEST_ISO) (cmdline: $(TEST_CMDLINE))"

# Headless autotests: rebuild userspace + image + test ISO, boot with the
# host-side watchdog (handles kernel crashes / SMP hangs via timeouts).
#   make test [SMP=n]          — full suite, 1 CPU by default
#   make test-smp [SMP=4]      — override CPU count
TEST_TIMEOUT ?= 240
TEST_STALL ?= 45

test: filesystem test-iso
	chmod +x tools/rbtest-host.sh
	./tools/rbtest-host.sh --iso $(TEST_ISO) --smp $(SMP) --timeout $(TEST_TIMEOUT) --stall $(TEST_STALL)

test-smp: filesystem test-iso
	chmod +x tools/rbtest-host.sh
	./tools/rbtest-host.sh --iso $(TEST_ISO) --smp $(SMP) --timeout $(TEST_TIMEOUT) --stall $(TEST_STALL)

# --- QEMU Execution ---

SMP ?= 1
QEMU_CPU ?= qemu64,+smep,+smap

run: iso filesystem
	@echo "=== Running rbOS in QEMU ==="
	qemu-system-x86_64 \
		-cpu $(QEMU_CPU) \
		-cdrom rbos.iso \
		-drive file=filesystem.img,format=raw,if=ide,cache=writethrough \
		-serial stdio \
		-m 256M \
		-boot d \
		-smp $(SMP)

run-no-disk: iso
	qemu-system-x86_64 -cpu $(QEMU_CPU) -cdrom rbos.iso -serial stdio -m 256M -boot d -smp $(SMP)

run-nographic: iso filesystem
	qemu-system-x86_64 \
		-cpu $(QEMU_CPU) \
		-cdrom rbos.iso \
		-drive file=filesystem.img,format=raw,if=ide,cache=writethrough \
		-nographic \
		-m 256M \
		-boot d \
		-smp $(SMP)

run-debug: iso
	qemu-system-x86_64 -cpu $(QEMU_CPU) -cdrom rbos.iso -serial stdio -m 256M -boot d -smp $(SMP) -d int,cpu_reset

# --- Everything (clean checkout -> bootable ISO) ---

world: libs user all filesystem iso
	@echo "✅ world build complete — run 'make run'"

# --- Cleanup ---

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) rbos.iso rbos-test.iso $(DISK_IMG)
	rm -rf build/disk-stage build/disk-rootfs.ext2 build/disk-fat.img build/disk-limine.conf
	$(CARGO) clean --manifest-path $(KERNEL_DIR)/Cargo.toml
	$(MAKE) -C $(USER_DIR) clean
	$(MAKE) -C ports clean

distclean: clean
	rm -rf $(SYSROOT) filesystem.img
	rm -rf mlibc/build-rbos-static mlibc/build-rbos-dynamic mlibc/rbos-*-cross.txt
	rm -rf mlibc-sysdeps/sysdeps/rbos/lib
	rm -rf ports/gnu/build
	@echo "✅ Full clean complete (sysroot, images, all build trees removed)"
	@echo "   Restore with 'make world' (third-party trees reconfigure from scratch)."

help:
	@echo "rbOS Makefile Targets:"
	@echo "  make             - Build kernel only"
	@echo ""
	@echo "Third-party components (build rarely):"
	@echo "  make mlibc       - Build static mlibc (libc.a)"
	@echo "  make mlibc-dynamic - Build dynamic mlibc (ld.so/libc.so)"
	@echo "  make ports       - Build & install ports (oksh, pixman)"
	@echo "  make libs        - All of the above"
	@echo ""
	@echo "Our userspace:"
	@echo "  make user        - Build userspace programs (rbit, oksh needs libs first)"
	@echo "  make host-tests  - Build+install host Linux/glibc probes (opt-in, big)"
	@echo "  make host-tests-remove - Remove them from sysroot again"
	@echo "  make tests-remove - Remove user tests + host probes from sysroot"
	@echo "                     (safe allowlist; DRY_RUN=1 only lists files)"
	@echo "  make -C user/tests install - Restore user test binaries"
	@echo ""
	@echo "Assemble & run:"
	@echo "  make filesystem  - Create Ext2 sysroot image (from installed bits)"
	@echo "  make filesystem NO_TESTS=1 - Same, without autotest binaries"
	@echo "                     (also removes stale tests from an old sysroot)"
	@echo "  make image       - Recreate image from the sysroot as it is now"
	@echo "  make iso         - Create bootable ISO"
	@echo "  make disk        - Create bootable HDD image (MBR+FAT32 boot+ext2 root)"
	@echo "  make disk-test   - Boot disk image headless, run suite (no cdrom)"
	@echo "  make run [SMP=n] - Run rbOS in QEMU"
	@echo "  make world [NO_TESTS=1] - Full build: libs + user + kernel + image + iso"
	@echo ""
	@echo "Autotests (no terminal needed, report on serial + files):"
	@echo "  make test [SMP=n] - Build test ISO + run suite headless (SMP=1 default)"
	@echo "  make test-smp SMP=4 - Same, with N CPUs (race/hang watchdog)"
	@echo "  make test-iso    - Only build rbos-test.iso (cmdline: console=ttyS0 rbtest)"
	@echo "  ./tools/rbtest-host.sh --iso rbos-test.iso --smp 4 --timeout 240"
	@echo "  In-guest manual: rbtest [--list] [--interactive] [name...]"
	@echo ""
	@echo "Cleanup:"
	@echo "  make clean       - Clean kernel and user builds"
	@echo "  make strip-sysroot - Strip debug info from sysroot binaries/libs"
	@echo "  make distclean   - Full clean including sysroot/image"
