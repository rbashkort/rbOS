#include <stdio.h>

/* Host-toolchain static-TLS smoke test (NOT mlibc/rbos-cc): exercises the
 * kernel ABI the way prebuilt Linux binaries do — static PT_TLS setup,
 * arch_prctl, brk/mmap growth. rbtest verdict: pass_mark "TLS-OK". */
__thread int tdata = 42;
__thread long counter;
__thread char buf[64];

int main(void) {
    counter = 123456789L;
    for (int i = 0; i < (int)sizeof(buf); i++)
        buf[i] = (char)(i + 1);
    printf("tls-main tdata=%d counter=%ld buf0=%d buf63=%d\n",
           tdata, counter, buf[0], buf[63]);
    if (tdata != 42 || counter != 123456789L || buf[0] != 1 || buf[63] != 64) {
        printf("TLS-FAIL\n");
        return 1;
    }
    printf("TLS-OK\n");
    return 0;
}
