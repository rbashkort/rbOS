# limine — bootloader binaries (vendored)

Only the files `make iso` copies into the ISO are kept here
(see `Makefile`, `iso` target):

- `bin/BOOTX64.EFI`
- `bin/limine-bios.sys`
- `bin/limine-bios-cd.bin`
- `bin/limine-uefi-cd.bin`

## Provenance

Upstream: https://github.com/limine-bootloader/limine, tag `v12.4.1`
(`0b5d1dc05b439217368e944b62add67cab565f9b`).

Byte-identical to the official release asset `limine-binary.tar.xz`
from https://github.com/limine-bootloader/limine/releases/tag/v12.4.1
(+ `.sig`, minisign). Re-verify any time:

```
curl -sL -o /tmp/lb.tar.xz \
  https://github.com/limine-bootloader/limine/releases/download/v12.4.1/limine-binary.tar.xz
tar -xf /tmp/lb.tar.xz -C /tmp
for f in BOOTX64.EFI limine-bios.sys limine-bios-cd.bin limine-uefi-cd.bin; do
  cmp bin/$f /tmp/limine-binary/$f && echo "OK $f"
done
```

The previous vendored source snapshot is gone on purpose: nothing in the
tree builds limine, the ISO recipe only copies these 4 files.

## License

`LICENSE` is the upstream BSD-2-Clause text (was `COPYING` in the source
snapshot, byte-identical). Redistribution of the binaries with this notice
is what the license asks for — keep the file next to `bin/`.
