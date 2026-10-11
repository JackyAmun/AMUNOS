#include <stdio.h>
#include <syscall.h>

int main(void)
{
    mem_info_t info;
    if (sys_mem_info(&info) < 0) {
        printf("MEM: MEMORY INFO UNAVAILABLE\n");
        return 1;
    }
    printf("AMUNOS KERNEL HEAP\n");
    printf("------------------\n");
    printf("TOTAL:        %u BYTES (%u KB)\n",
           info.total_bytes, info.total_bytes / 1024);
    printf("FREE:         %u BYTES (%u KB)\n",
           info.free_bytes, info.free_bytes / 1024);
    printf("USED:         %u BYTES (%u KB)\n",
           info.total_bytes - info.free_bytes,
           (info.total_bytes - info.free_bytes) / 1024);
    printf("LARGEST FREE: %u BYTES (%u KB)\n",
           info.largest_free_bytes, info.largest_free_bytes / 1024);
    printf("FREE BLOCKS:  %u\n", info.free_blocks);
    return 0;
}
