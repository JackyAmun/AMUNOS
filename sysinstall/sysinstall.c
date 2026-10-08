/* sysinstall - first AMUNOS installation media prototype.
 *
 * The program is intentionally text based, like a small DOS installer.  The
 * booted medium is the source; the user selects a discovered physical IDE or
 * floppy device as the destination.  The kernel raw-sector calls are limited
 * to one sector per operation and never expose CD/AHCI writes.
 */
#include <stdio.h>
#include <string.h>
#include <syscall.h>

static const char *slot_name(int slot)
{
    static const char *names[] = {
        "IDE0 master", "IDE0 slave", "IDE1 master", "IDE1 slave", "FDC"
    };
    return slot >= 0 && slot < 5 ? names[slot] : "device";
}

static int read_line_key(void)
{
    int c, value = 0;
    for (;;) {
        c = sys_getkey();
        if (c == 27 || c == 3) return c;
        if (c == '\r' || c == '\n') {
            putchar('\n');
            fflush(stdout);
            return value;
        }
        if (c == '\b' || c == 127) {
            value = 0;
            putchar('\b'); putchar(' '); putchar('\b');
            fflush(stdout);
            continue;
        }
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c >= 32 && c <= 126) {
            value = c;
            putchar(c);
            fflush(stdout);
        }
    }
}

static int user_cancelled(void)
{
    int c;
    if (!sys_keyhit()) return 0;
    c = sys_getkey();
    return c == 27 || c == 3;
}

static int choose_target(const sysinfo_t *si)
{
    int slot;
    printf("\nWritable installation targets:\n");
    for (slot = 0; slot < 5; slot++) {
        if (!si->dev_present[slot] || !si->dev_sectors[slot]) continue;
        printf("  %d) %-12s %u sectors (%u MB)\n", slot + 1,
               slot_name(slot), si->dev_sectors[slot],
               si->dev_sectors[slot] / 2048);
    }
    printf("Select target number, or ESC to cancel: ");
    fflush(stdout);
    for (;;) {
        int c = read_line_key();
        if (c == 27 || c == 3) return -1;
        if (c >= '1' && c <= '5') {
            slot = c - '1';
            if (si->dev_present[slot] && si->dev_sectors[slot]) return slot;
        }
        putchar('\a');
    }
}

static int install_mbr_code(int target)
{
    int fd, n;
    unsigned char mbr[512], code[446];
    fd = sys_open("MBR.BIN", O_RDONLY);
    if (fd < 0) return -1;
    n = (int)sys_read(fd, code, sizeof code);
    sys_close(fd);
    if (n != (int)sizeof code || sys_blk_read(target, 0, mbr) < 0)
        return -1;
    for (int i = 0; i < (int)sizeof code; i++) mbr[i] = code[i];
    return sys_blk_write(target, 0, mbr);
}

int main(void)
{
    sysinfo_t si;
    unsigned char sector[512];
    unsigned char verify[512];
    unsigned int total, lba, target_lba;
    int target;
    int c;

    printf("\nAMUNOS SYSINSTALL 0.1\n");
    printf("=====================\n");
    if (sys_sysinfo(&si) < 0 || si.current_sectors == 0) {
        printf("Cannot determine the boot source capacity.\n");
        return 1;
    }
    printf("Source: %c: (%u sectors)\n", si.drive_letter,
           si.current_sectors);
    printf("This installer copies the bootable AMUNOS volume to another disk.\n");
    printf("All files on the selected target will be overwritten.\n");

    target = choose_target(&si);
    if (target < 0) {
        printf("Installation cancelled.\n");
        return 0;
    }
    if (target == si.current_drive && target < 5) {
        printf("Refusing to overwrite the boot source.\n");
        return 1;
    }
    if (si.dev_sectors[target] < si.current_sectors) {
        printf("Target is too small: need %u sectors.\n", si.current_sectors);
        return 1;
    }
    target_lba = target == 4 ? 0 : 2048;
    if (si.dev_sectors[target] <= target_lba ||
        si.dev_sectors[target] - target_lba < si.current_sectors) {
        printf("Target partition is too small for the system volume.\n");
        return 1;
    }

    printf("\nTarget: %s, %u sectors\n", slot_name(target),
           si.dev_sectors[target]);
    printf("Format target before copying? (Y/N): ");
    fflush(stdout);
    c = read_line_key();
    if (c == 'Y') {
        printf("Formatting target...\n");
        if (sys_format(target) < 0) {
            printf("Format failed; installation cancelled.\n");
            return 1;
        }
        printf("Format complete.\n");
    } else if (c != 'N') {
        printf("Installation cancelled.\n");
        return 0;
    }
    printf("Type Y to begin copying, any other key to cancel: ");
    fflush(stdout);
    c = read_line_key();
    if (c != 'Y') { printf("Installation cancelled.\n"); return 0; }

    total = si.current_sectors;
    printf("Copying system volume; do not power off...\n");
    for (lba = 0; lba < total; lba++) {
        if (user_cancelled()) {
            printf("\nInstallation aborted.\n");
            return 0;
        }
        if (sys_blk_read(si.current_drive, lba, sector) < 0) {
            printf("Read failed at sector %u.\n", lba);
            return 1;
        }
        if (lba == 0 && target_lba != 0) {
            sector[28] = (unsigned char)(target_lba & 0xFF);
            sector[29] = (unsigned char)((target_lba >> 8) & 0xFF);
            sector[30] = (unsigned char)((target_lba >> 16) & 0xFF);
            sector[31] = (unsigned char)((target_lba >> 24) & 0xFF);
        }
        if (sys_blk_write(target, target_lba + lba, sector) < 0) {
            printf("Write failed at sector %u.\n", lba);
            return 1;
        }
        if ((lba & 31) == 0 || lba + 1 == total)
            printf("Copied %u/%u sectors\r", lba + 1, total);
    }
    putchar('\n');

    /* The final source sector is no longer in sector; verify the boot sector
     * separately so a successful copy is checked deterministically. */
    if (sys_blk_read(si.current_drive, 0, sector) < 0) {
        printf("Verification failed.\n");
        return 1;
    }
    if (target_lba != 0) {
        sector[28] = (unsigned char)(target_lba & 0xFF);
        sector[29] = (unsigned char)((target_lba >> 8) & 0xFF);
        sector[30] = (unsigned char)((target_lba >> 16) & 0xFF);
        sector[31] = (unsigned char)((target_lba >> 24) & 0xFF);
    }
    if (sys_blk_read(target, target_lba, verify) < 0 ||
        memcmp(sector, verify, sizeof sector) != 0) {
        printf("Verification failed.\n");
        return 1;
    }
    if (target_lba != 0 && install_mbr_code(target) < 0) {
        printf("MBR boot code installation failed.\n");
        return 1;
    }
    printf("Installation complete. Remove the installer disk and reboot.\n");
    return 0;
}
