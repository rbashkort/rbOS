#include <stdio.h>

int strlenn(char *s) {
    int len = 0;
    while (s[len] != '\0') { len++; };
    return len;
}

void print_cow(void) {
    printf("        \\   ^__^\n");
    printf("         \\  (oo)\\_______\n");
    printf("            (__)\\       )\\/\\\n");
    printf("                ||----w |\n");
    printf("                ||     ||\n");
}

void print_bubble(char *text) {
    int len = strlenn(text);
    for (int i = 0; i < len + 2; i++) printf("_");
    printf("\n");

    printf("< %s >\n", text);

    for (int i = 0; i < len + 2; i++) printf("-");
    printf("\n");
}



int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <message>\n", argv[0]);
        return 1;
    }

    print_bubble(argv[1]);
    print_cow();

    return 0;
}
