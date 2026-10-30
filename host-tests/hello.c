#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    char *p = malloc(64);
    if (!p)
        return 1;
    snprintf(p, 64, "%s:%d", argv[0], argc);
    printf("HELLO-OK %s pid=%d\n", p, (int)getpid());
    free(p);
    return 0;
}
