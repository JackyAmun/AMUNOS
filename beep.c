#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

int main(int argc, char **argv)
{
    int frequency = argc > 1 ? atoi(argv[1]) : 880;
    int duration = argc > 2 ? atoi(argv[2]) : 300;
    if (sys_beep((unsigned)frequency, (unsigned)duration) != 0) {
        printf("Usage: BEEP [frequency 37..20000] [duration_ms 1..5000]\n");
        return 1;
    }
    printf("BEEP %d Hz for %d ms\n", frequency, duration);
    return 0;
}
