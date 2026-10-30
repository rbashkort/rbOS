#include <stdint.h>

#define SYS_WRITE 1
#define SYS_EXIT  2

static inline int64_t syscall3(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall1(uint64_t n, uint64_t a1) {
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static int strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void print(const char* s) {
    syscall3(SYS_WRITE, 1, (uint64_t)s, strlen(s));
}

static void print_bubble(char *text) {
    int len = strlen(text);
    for (int i = 0; i < len + 2; i++) print("_");
    print("\n");

    print("< ");
    print(text);
    print(" >\n");

    for (int i = 0; i < len + 2; i++) print("-");
    print("\n");
}

static void print_cow(void) {
    print("        \\   ^__^\n");
    print("         \\  (oo)\\_______\n");
    print("            (__)\\       )\\/\\\n");
    print("                ||----w |\n");
    print("                ||     ||\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print("Usage: cowsay <message>\n");
        syscall1(SYS_EXIT, 1);
    }

    print_bubble(argv[1]);
    print_cow();

    syscall1(SYS_EXIT, 0);
    return 0;
}
