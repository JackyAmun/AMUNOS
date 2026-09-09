#include <stdio.h>
#include <syscall.h>

static const char *dev_name(int i)
{
    static const char *names[] = {
        "IDE0-M", "IDE0-S", "IDE1-M", "IDE1-S", "FDC", "ATAPI", "AHCI"
    };
    return (i >= 0 && i < 7) ? names[i] : "?";
}

int main(void)
{
    sysinfo_t si;
    if (sys_sysinfo(&si) < 0) {
        printf("SYSINFO: syscall failed\n");
        return 1;
    }

    printf("AMUNOS SYSINFO\n");
    printf("--------------\n");
    printf("ticks       : %u\n", si.ticks);
    printf("drive       : %c: slot=%d cwd_cluster=%d\n",
           si.drive_letter, si.current_drive, si.cwd_cluster);
    printf("filesystem  : FAT%d spc=%d root_lba=%d data_lba=%d\n",
           si.fs_fat_bits, si.fs_spc, si.fs_root_lba, si.fs_data_lba);
    printf("graphics    : framebuffer=%s gui=%s\n",
           si.fb_active ? "on" : "off", si.gui_active ? "on" : "off");
    printf("\nDEVICES\n");
    for (int i = 0; i < 7; i++) {
        if (!si.dev_present[i]) continue;
        printf("  %s  sectors=%u  model=%s\n",
               dev_name(i), si.dev_sectors[i], si.dev_model[i]);
    }
    return 0;
}
