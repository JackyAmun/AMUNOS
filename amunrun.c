#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char *msg = (char *)malloc(80);
    printf("AMUNOS TCC user program\n");
    printf("argc=%d\n", argc);
    if (!msg) {
        printf("malloc failed\n");
        return 1;
    }
    strcpy(msg, "compiled inside AMUNOS with TinyCC");
    printf("%s\n", msg);
    free(msg);
    return 0;
}
