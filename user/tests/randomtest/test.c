#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int all_bytes(const unsigned char *buf, size_t len, unsigned char value) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != value)
            return 0;
    }
    return 1;
}

int main(void) {
    unsigned char a[64];
    unsigned char b[64];
    unsigned char big[700];

    memset(a, 0, sizeof(a));
    memset(b, 0xff, sizeof(b));
    memset(big, 0xaa, sizeof(big));

    arc4random_buf(a, sizeof(a));
    arc4random_buf(b, sizeof(b));
    arc4random_buf(big, sizeof(big));

    if (all_bytes(a, sizeof(a), 0)) {
        printf("RANDOM-FAIL: arc4random_buf returned all zeroes\n");
        return 1;
    }
    if (memcmp(a, b, sizeof(a)) == 0) {
        printf("RANDOM-FAIL: two buffers are identical\n");
        return 1;
    }
    if (all_bytes(big, sizeof(big), 0xaa)) {
        printf("RANDOM-FAIL: large buffer was not filled\n");
        return 1;
    }

    /* A zero-length request must be harmless. */
    arc4random_buf(big, 0);

    if (arc4random_uniform(0) != 0 || arc4random_uniform(1) != 0) {
        printf("RANDOM-FAIL: uniform(0)/uniform(1) must be zero\n");
        return 1;
    }

    for (unsigned i = 0; i < 4096; i++) {
        uint32_t v = arc4random_uniform(10);
        if (v >= 10) {
            printf("RANDOM-FAIL: uniform(10) out of range: %u\n",
                   (unsigned)v);
            return 1;
        }
    }

    /* Exercise the scalar entry point too. */
    (void)arc4random();

    printf("RANDOM-OK\n");
    return 0;
}
