/* fdc.c - 82077-compatible floppy controller.
 *
 * The driver keeps the simple AMUNOS polling backend, but now exposes device
 * errors, retries failed commands, detects common FAT floppy geometries and
 * supports sector writes. Reads use polled non-DMA PIO; writes use 8237 DMA
 * channel 2. IRQ6 and a sleeping request queue remain later optimizations.
 */
#include "common.h"
#include "fdc.h"

#define FDC_DOR   0x3F2
#define FDC_MSR   0x3F4
#define FDC_FIFO  0x3F5
#define FDC_DIR   0x3F7
#define FDC_CCR   0x3F7

#define FDC_RQM   0x80
#define FDC_DIO   0x40

#define ST1_NW    0x02
#define ST1_ND    0x04
#define ST1_OR    0x10
#define ST1_DE    0x20
#define ST2_DD    0x20

#define DMA_MASK       0x0A
#define DMA_MODE       0x0B
#define DMA_CLEAR_FF   0x0C
#define DMA_CH2_ADDR   0x04
#define DMA_CH2_COUNT  0x05
#define DMA_CH2_PAGE   0x81

typedef struct {
    unsigned short sectors_per_track;
    unsigned short heads;
    unsigned short tracks;
    unsigned char ccr;
    const char *name;
} fdc_geometry_t;

static fdc_geometry_t geometry = { 18, 2, 80, 0, "FLOPPY 1.44M" };
static int fdc_ok;
static unsigned char dma_buffer[512] __attribute__((aligned(512)));

static void fdc_log_number(unsigned value)
{
    char digits[12];
    int n = 0;
    if (!value) { serial_putc('0'); return; }
    while (value) { digits[n++] = (char)('0' + value % 10); value /= 10; }
    while (n) serial_putc(digits[--n]);
}

static int msr_wait(unsigned mask, unsigned value, unsigned limit)
{
    unsigned i;
    for (i = 0; i < limit; i++) {
        unsigned status = io_in8(FDC_MSR);
        if ((status & mask) == value) return BLK_OK;
        if ((i & 0xFFF) == 0xFFF) io_in8(0x80);
    }
    return BLK_ERR_BUSY;
}

static int fdc_out(unsigned char value)
{
    int rc = msr_wait(FDC_RQM | FDC_DIO, FDC_RQM, 250000);
    if (rc != BLK_OK) return rc;
    io_out8(FDC_FIFO, value);
    return BLK_OK;
}

static int fdc_in(unsigned char *value)
{
    int rc = msr_wait(FDC_RQM | FDC_DIO, FDC_RQM | FDC_DIO, 250000);
    if (rc != BLK_OK) return rc;
    *value = io_in8(FDC_FIFO);
    return BLK_OK;
}

static int fdc_sense(unsigned char *st0, unsigned char *pcn)
{
    int rc;
    if ((rc = fdc_out(0x08)) != BLK_OK) return rc;
    if ((rc = fdc_in(st0)) != BLK_OK) return rc;
    return fdc_in(pcn);
}

static int fdc_specify(int non_dma)
{
    int rc;
    if ((rc = fdc_out(0x03)) != BLK_OK) return rc;
    if ((rc = fdc_out(0xCF)) != BLK_OK) return rc;
    return fdc_out((unsigned char)(0x02 | (non_dma ? 1 : 0)));
}

/* 8237 DMA channel 2. write_to_disk means memory -> FDC (mode 0x4A). */
static int fdc_dma_program(int write_to_disk)
{
    unsigned address = (unsigned)dma_buffer;
    unsigned count = sizeof(dma_buffer) - 1;
    unsigned flags;
    if (address >= 0x1000000u || ((address & 0xFFFFu) + 511u) > 0xFFFFu)
        return BLK_ERR_RANGE;

    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    io_out8(DMA_MASK, 0x06);              /* mask channel 2 */
    io_out8(DMA_CLEAR_FF, 0xFF);
    io_out8(DMA_CH2_ADDR, (unsigned char)address);
    io_out8(DMA_CH2_ADDR, (unsigned char)(address >> 8));
    io_out8(DMA_CH2_PAGE, (unsigned char)(address >> 16));
    io_out8(DMA_CLEAR_FF, 0xFF);
    io_out8(DMA_CH2_COUNT, (unsigned char)count);
    io_out8(DMA_CH2_COUNT, (unsigned char)(count >> 8));
    io_out8(DMA_MODE, (unsigned char)(write_to_disk ? 0x4A : 0x46));
    io_out8(DMA_MASK, 0x02);              /* unmask channel 2 */
    if (flags & 0x200) __asm__ volatile("sti" : : : "memory");
    return BLK_OK;
}

static int fdc_recalibrate(void)
{
    unsigned char st0, pcn;
    int i, rc;
    if ((rc = fdc_out(0x07)) != BLK_OK) return rc;
    if ((rc = fdc_out(0x00)) != BLK_OK) return rc;
    for (i = 0; i < 16; i++) {
        if (fdc_sense(&st0, &pcn) != BLK_OK) continue;
        if ((st0 & 0x20) && pcn == 0) return BLK_OK;
    }
    return BLK_ERR_SEEK;
}

static int fdc_seek(unsigned cylinder, unsigned head)
{
    unsigned char st0, pcn;
    int i, rc;
    if ((rc = fdc_out(0x0F)) != BLK_OK) return rc;
    if ((rc = fdc_out((unsigned char)(head << 2))) != BLK_OK) return rc;
    if ((rc = fdc_out((unsigned char)cylinder)) != BLK_OK) return rc;
    for (i = 0; i < 16; i++) {
        if (fdc_sense(&st0, &pcn) != BLK_OK) continue;
        if ((st0 & 0x20) && pcn == cylinder) return BLK_OK;
    }
    return BLK_ERR_SEEK;
}

static int fdc_reset(void)
{
    unsigned char st0, pcn;
    int i;

    fdc_ok = 0;
    io_out8(FDC_DOR, 0x00);
    for (i = 0; i < 1000; i++) io_in8(0x80);
    io_out8(FDC_DOR, 0x1C); /* controller enabled, motor A on */
    io_out8(FDC_CCR, geometry.ccr);
    if (msr_wait(FDC_RQM, FDC_RQM, 500000) != BLK_OK) return BLK_ERR_NODEV;

    for (i = 0; i < 4; i++) {
        if (fdc_sense(&st0, &pcn) != BLK_OK) return BLK_ERR_IO;
    }
    /* SRT/HUT plus HLT=2ms, ND=1. */
    if (fdc_specify(1) != BLK_OK) return BLK_ERR_IO;
    if (fdc_recalibrate() != BLK_OK) return BLK_ERR_SEEK;
    fdc_ok = 1;
    return BLK_OK;
}

static int fdc_result_error(const unsigned char st[7])
{
    if ((st[0] & 0xC0) == 0 && st[1] == 0 && st[2] == 0) return BLK_OK;
    serial_puts("[FDC] result st0="); fdc_log_number(st[0]);
    serial_puts(" st1="); fdc_log_number(st[1]);
    serial_puts(" st2="); fdc_log_number(st[2]); serial_putc('\n');
    if (st[1] & ST1_NW) return BLK_ERR_READ_ONLY;
    if ((st[1] & ST1_DE) || (st[2] & ST2_DD)) return BLK_ERR_CRC;
    if (st[1] & ST1_ND) return BLK_ERR_SEEK;
    if (st[1] & ST1_OR) return BLK_ERR_IO;
    return BLK_ERR_IO;
}

static int fdc_transfer_once(unsigned lba, void *buffer, int write)
{
    unsigned cylinder, head, sector, i;
    unsigned char result[7];
    unsigned char *bytes = (unsigned char *)buffer;
    int rc;

    cylinder = lba / (geometry.sectors_per_track * geometry.heads);
    head = (lba / geometry.sectors_per_track) % geometry.heads;
    sector = lba % geometry.sectors_per_track + 1;
    if (cylinder >= geometry.tracks) return BLK_ERR_RANGE;

    if ((rc = fdc_seek(cylinder, head)) != BLK_OK) return rc;
    if (write) {
        for (i = 0; i < 512; i++) dma_buffer[i] = bytes[i];
        if ((rc = fdc_specify(0)) != BLK_OK) return rc;
        if ((rc = fdc_dma_program(1)) != BLK_OK) {
            fdc_specify(1);
            return rc;
        }
    }
    if ((rc = fdc_out(write ? 0x45 : 0x46)) != BLK_OK) goto command_failed;
    if ((rc = fdc_out((unsigned char)(head << 2))) != BLK_OK) goto command_failed;
    if ((rc = fdc_out((unsigned char)cylinder)) != BLK_OK) goto command_failed;
    if ((rc = fdc_out((unsigned char)head)) != BLK_OK) goto command_failed;
    if ((rc = fdc_out((unsigned char)sector)) != BLK_OK) goto command_failed;
    if ((rc = fdc_out(2)) != BLK_OK) goto command_failed; /* 512-byte sector */
    if ((rc = fdc_out((unsigned char)sector)) != BLK_OK) goto command_failed;
    if ((rc = fdc_out(0x1B)) != BLK_OK) goto command_failed;
    if ((rc = fdc_out(0xFF)) != BLK_OK) goto command_failed;

    if (!write) {
        for (i = 0; i < 512; i++) {
            rc = msr_wait(FDC_RQM | FDC_DIO, FDC_RQM | FDC_DIO, 250000);
            if (rc != BLK_OK) {
                /* No-media probing retries this path; report the final
                 * mount result once instead of flooding the boot log. */
                return rc;
            }
            bytes[i] = io_in8(FDC_FIFO);
        }
    }
    for (i = 0; i < 7; i++) {
        if ((rc = fdc_in(&result[i])) != BLK_OK) {
            if (write) fdc_specify(1);
            return rc;
        }
    }
    rc = fdc_result_error(result);
    if (write && fdc_specify(1) != BLK_OK && rc == BLK_OK) rc = BLK_ERR_IO;
    return rc;

command_failed:
    if (write) fdc_specify(1);
    return rc;
}

static int fdc_transfer(unsigned lba, unsigned count, void *buffer, int write)
{
    unsigned n;
    unsigned char *bytes = (unsigned char *)buffer;
    unsigned capacity = fdc_capacity();
    if (!fdc_ok) return BLK_ERR_NODEV;
    if (!buffer || !count) return BLK_ERR_IO;
    if (lba >= capacity || count > capacity - lba) return BLK_ERR_RANGE;

    for (n = 0; n < count; n++) {
        int attempt, rc = BLK_ERR_IO;
        for (attempt = 0; attempt < 3; attempt++) {
            rc = fdc_transfer_once(lba + n, bytes + n * 512, write);
            if (rc == BLK_OK || rc == BLK_ERR_READ_ONLY) break;
            if (attempt == 0) fdc_recalibrate();
            else fdc_reset();
        }
        if (rc != BLK_OK) {
            serial_puts(write ? "[FDC] write failed rc=" : "[FDC] read failed rc=");
            fdc_log_number((unsigned)(-rc));
            serial_puts(" lba="); fdc_log_number(lba + n); serial_putc('\n');
            return rc;
        }
    }
    return BLK_OK;
}

int fdc_init(void)
{
    geometry.sectors_per_track = 18;
    geometry.heads = 2;
    geometry.tracks = 80;
    geometry.ccr = 0;
    geometry.name = "FLOPPY 1.44M";
    return fdc_reset();
}

int fdc_ready(void) { return fdc_ok; }
unsigned int fdc_capacity(void)
{
    return (unsigned)geometry.sectors_per_track * geometry.heads * geometry.tracks;
}
const char *fdc_media_name(void) { return geometry.name; }

int fdc_media_changed(void)
{
    if (!fdc_ok) return BLK_ERR_NODEV;
    return (io_in8(FDC_DIR) & 0x80) ? 1 : 0;
}

int fdc_probe_media(void *boot_sector)
{
    static const fdc_geometry_t probes[] = {
        { 18, 2, 80, 0, "FLOPPY 1.44M" },
        { 15, 2, 80, 0, "FLOPPY 1.20M" },
        {  9, 2, 80, 2, "FLOPPY 720K" },
        {  9, 2, 40, 2, "FLOPPY 360K" }
    };
    unsigned char local[512];
    unsigned char *b = boot_sector ? (unsigned char *)boot_sector : local;
    unsigned i;

    if (!fdc_ok) return BLK_ERR_NODEV;
    for (i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        unsigned short bps, spt, heads;
        unsigned total;
        geometry = probes[i];
        io_out8(FDC_CCR, geometry.ccr);
        fdc_recalibrate();
        if (fdc_transfer_once(0, b, 0) != BLK_OK) continue;

        bps = (unsigned short)(b[11] | ((unsigned short)b[12] << 8));
        spt = (unsigned short)(b[24] | ((unsigned short)b[25] << 8));
        heads = (unsigned short)(b[26] | ((unsigned short)b[27] << 8));
        total = (unsigned)(b[19] | ((unsigned)b[20] << 8));
        if (!total)
            total = (unsigned)b[32] | ((unsigned)b[33] << 8) |
                    ((unsigned)b[34] << 16) | ((unsigned)b[35] << 24);
        if (bps == 512 && spt >= 8 && spt <= 36 && heads >= 1 && heads <= 2 &&
            total >= (unsigned)spt * heads) {
            geometry.sectors_per_track = spt;
            geometry.heads = heads;
            geometry.tracks = (unsigned short)(total / ((unsigned)spt * heads));
        }
        return BLK_OK;
    }
    geometry.sectors_per_track = 18;
    geometry.heads = 2;
    geometry.tracks = 80;
    geometry.ccr = 0;
    geometry.name = "FLOPPY 1.44M";
    return BLK_ERR_NO_MEDIA;
}

int fdc_read_sectors(unsigned lba, unsigned count, void *buf)
{
    return fdc_transfer(lba, count, buf, 0);
}

int fdc_write_sectors(unsigned lba, unsigned count, const void *buf)
{
    return fdc_transfer(lba, count, (void *)buf, 1);
}
