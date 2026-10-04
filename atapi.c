/* atapi.c - legacy IDE ATAPI packet device support.
 *
 * Scans both primary/secondary channels and master/slave positions. The block
 * interface exposes 512-byte logical windows over 2048-byte optical sectors.
 * CD-ROM media is intentionally read-only; recording requires a separate MMC
 * writer, cache management and disc finalization implementation.
 */
#include "common.h"
#include "atapi.h"

#define ATA_DATA  0
#define ATA_ERR   1
#define ATA_FEAT  1
#define ATA_SCNT  2
#define ATA_LBAM  3
#define ATA_LBAH  4
#define ATA_DH    6
#define ATA_STAT  7
#define ATA_CMD   7

#define ATA_BSY   0x80
#define ATA_DRQ   0x08
#define ATA_DF    0x20
#define ATA_ERRF  0x01

static unsigned short at_base = 0x170;
static unsigned short at_ctl = 0x376;
static unsigned char at_slave = 1;
static int atapi_ok;
static int media_ok;
static unsigned int atapi_sectors;
static unsigned char sense_key, sense_asc, sense_ascq;
static char model[21] = "ATAPI CD-ROM";
static unsigned char sector_window[2048];
static unsigned int window_lba = 0xFFFFFFFFu;

static void ata_delay(void)
{
    io_in8(at_ctl); io_in8(at_ctl); io_in8(at_ctl); io_in8(at_ctl);
}

static void ata_select(unsigned char slave)
{
    io_out8(at_base + ATA_DH, (unsigned char)(0xA0 | (slave ? 0x10 : 0)));
    ata_delay();
}

static int ata_wait_not_busy(unsigned limit)
{
    unsigned i;
    for (i = 0; i < limit; i++) {
        unsigned char status = io_in8(at_base + ATA_STAT);
        if (status == 0xFF) return BLK_ERR_NODEV;
        if (!(status & ATA_BSY)) return BLK_OK;
        if ((i & 0xFFF) == 0xFFF) io_in8(0x80);
    }
    return BLK_ERR_BUSY;
}

static int ata_wait_phase(unsigned limit, unsigned char *status)
{
    unsigned i;
    for (i = 0; i < limit; i++) {
        unsigned char st = io_in8(at_base + ATA_STAT);
        if (st == 0xFF) return BLK_ERR_NODEV;
        if (!(st & ATA_BSY)) {
            *status = st;
            if (st & (ATA_ERRF | ATA_DF)) return BLK_ERR_IO;
            return BLK_OK;
        }
        if ((i & 0xFFF) == 0xFFF) io_in8(0x80);
    }
    return BLK_ERR_BUSY;
}

static int ata_wait_drq(unsigned limit)
{
    unsigned i;
    for (i = 0; i < limit; i++) {
        unsigned char status = io_in8(at_base + ATA_STAT);
        if (status == 0xFF) return BLK_ERR_NODEV;
        if (!(status & ATA_BSY) && (status & ATA_DRQ)) return BLK_OK;
        if (!(status & ATA_BSY) && (status & (ATA_ERRF | ATA_DF))) return BLK_ERR_IO;
        if ((i & 0xFFF) == 0xFFF) io_in8(0x80);
    }
    return BLK_ERR_BUSY;
}

static int atapi_packet_raw(const unsigned char packet[12], void *buffer,
                            unsigned expected_bytes)
{
    unsigned char status;
    unsigned char *dst = (unsigned char *)buffer;
    unsigned received = 0;
    unsigned phase_guard = 0;
    int rc;

    ata_select(at_slave);
    if ((rc = ata_wait_not_busy(600000)) != BLK_OK) return rc;
    io_out8(at_base + ATA_FEAT, 0);       /* PIO, no overlap */
    io_out8(at_base + ATA_SCNT, 0);
    io_out8(at_base + ATA_LBAM, 0xFE);
    io_out8(at_base + ATA_LBAH, 0xFF);
    io_out8(at_ctl, 0x02);                /* nIEN: polling mode */
    io_out8(at_base + ATA_CMD, 0xA0);     /* PACKET */
    if ((rc = ata_wait_drq(600000)) != BLK_OK) return rc;

    for (unsigned i = 0; i < 6; i++) {
        unsigned short word = (unsigned short)packet[i * 2] |
                              ((unsigned short)packet[i * 2 + 1] << 8);
        io_out16(at_base + ATA_DATA, word);
    }

    while (phase_guard++ < 32) {
        if ((rc = ata_wait_phase(1200000, &status)) != BLK_OK) return rc;
        if (!(status & ATA_DRQ))
            return received >= expected_bytes ? BLK_OK : BLK_ERR_IO;
        {
            unsigned bytes = io_in8(at_base + ATA_LBAM) |
                             ((unsigned)io_in8(at_base + ATA_LBAH) << 8);
            unsigned words;
            if (!bytes) bytes = 0x10000u;
            words = (bytes + 1) / 2;
            for (unsigned i = 0; i < words; i++) {
                unsigned short word = io_in16(at_base + ATA_DATA);
                if (received < expected_bytes && dst) dst[received] = (unsigned char)word;
                received++;
                if (received < expected_bytes && dst) dst[received] = (unsigned char)(word >> 8);
                received++;
            }
        }
    }
    return BLK_ERR_BUSY;
}

static void atapi_request_sense(void)
{
    unsigned char packet[12] = { 0x03, 0, 0, 0, 18, 0, 0, 0, 0, 0, 0, 0 };
    unsigned char sense[18];
    sense_key = sense_asc = sense_ascq = 0;
    if (atapi_packet_raw(packet, sense, sizeof(sense)) == BLK_OK) {
        sense_key = sense[2] & 0x0F;
        sense_asc = sense[12];
        sense_ascq = sense[13];
    }
}

static int atapi_test_unit_ready(void)
{
    unsigned char packet[12] = { 0 };
    int rc = atapi_packet_raw(packet, 0, 0);
    if (rc == BLK_OK) return BLK_OK;
    atapi_request_sense();
    if (sense_key == 0x02 || sense_asc == 0x3A) return BLK_ERR_NO_MEDIA;
    if (sense_key == 0x06) return BLK_ERR_BUSY; /* unit attention: retry */
    return rc;
}

static int atapi_read_capacity(void)
{
    unsigned char packet[12] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    unsigned char capacity[8];
    unsigned block_size;
    int rc = atapi_packet_raw(packet, capacity, sizeof(capacity));
    if (rc != BLK_OK) return rc;
    atapi_sectors = ((unsigned)capacity[0] << 24) |
                    ((unsigned)capacity[1] << 16) |
                    ((unsigned)capacity[2] << 8) | capacity[3];
    atapi_sectors++;
    block_size = ((unsigned)capacity[4] << 24) |
                 ((unsigned)capacity[5] << 16) |
                 ((unsigned)capacity[6] << 8) | capacity[7];
    if (block_size != 2048 || !atapi_sectors) return BLK_ERR_IO;
    return BLK_OK;
}

static void atapi_decode_model(const unsigned short id[256])
{
    const unsigned char *src = (const unsigned char *)id + 54;
    int i, end = 20;
    for (i = 0; i < 20; i++) model[i] = src[(i & ~1) + 1 - (i & 1)];
    while (end > 0 && model[end - 1] == ' ') end--;
    model[end] = 0;
    if (!model[0]) strcpy(model, "ATAPI CD-ROM");
}

int atapi_probe(void)
{
    static const unsigned short bases[2] = { 0x1F0, 0x170 };
    static const unsigned short controls[2] = { 0x3F6, 0x376 };
    unsigned short id[256];

    atapi_ok = 0;
    media_ok = 0;
    atapi_sectors = 0;
    window_lba = 0xFFFFFFFFu;

    for (unsigned channel = 0; channel < 2; channel++) {
        for (unsigned slave = 0; slave < 2; slave++) {
            unsigned char status;
            at_base = bases[channel];
            at_ctl = controls[channel];
            at_slave = (unsigned char)slave;
            ata_select(at_slave);
            status = io_in8(at_base + ATA_STAT);
            if (status == 0 || status == 0xFF) continue;
            if (ata_wait_not_busy(300000) != BLK_OK) continue;
            io_out8(at_base + ATA_SCNT, 0);
            io_out8(at_base + ATA_LBAM, 0);
            io_out8(at_base + ATA_LBAH, 0);
            io_out8(at_base + ATA_CMD, 0xA1); /* IDENTIFY PACKET DEVICE */
            if (ata_wait_drq(600000) != BLK_OK) continue;
            for (unsigned i = 0; i < 256; i++) id[i] = io_in16(at_base + ATA_DATA);
            if (ata_wait_not_busy(300000) != BLK_OK || !(id[0] & 0x8000)) continue;
            atapi_decode_model(id);
            atapi_ok = 1;
            atapi_refresh_media();
            return BLK_OK;
        }
    }
    return BLK_ERR_NODEV;
}

int atapi_ready(void) { return atapi_ok; }
int atapi_media_present(void) { return media_ok; }
unsigned int atapi_capacity(void) { return atapi_sectors; }
const char *atapi_model(void) { return model; }

int atapi_refresh_media(void)
{
    int rc;
    if (!atapi_ok) return BLK_ERR_NODEV;
    window_lba = 0xFFFFFFFFu;
    media_ok = 0;
    atapi_sectors = 0;
    rc = atapi_test_unit_ready();
    if (rc == BLK_ERR_BUSY) rc = atapi_test_unit_ready();
    if (rc != BLK_OK) return rc;
    rc = atapi_read_capacity();
    if (rc != BLK_OK) {
        atapi_request_sense();
        return rc;
    }
    media_ok = 1;
    return BLK_OK;
}

int atapi_read_sectors_2048(unsigned lba, unsigned count, void *buf)
{
    unsigned char *dst = (unsigned char *)buf;
    if (!atapi_ok) return BLK_ERR_NODEV;
    if (!media_ok) return BLK_ERR_NO_MEDIA;
    if (!buf || !count) return BLK_ERR_IO;
    if (lba >= atapi_sectors || count > atapi_sectors - lba) return BLK_ERR_RANGE;

    for (unsigned n = 0; n < count; n++) {
        unsigned block = lba + n;
        unsigned char packet[12] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
        int rc;
        packet[2] = (unsigned char)(block >> 24);
        packet[3] = (unsigned char)(block >> 16);
        packet[4] = (unsigned char)(block >> 8);
        packet[5] = (unsigned char)block;
        rc = atapi_packet_raw(packet, dst + n * 2048, 2048);
        if (rc != BLK_OK) {
            atapi_request_sense();
            if (sense_key == 0x02 || sense_asc == 0x3A) {
                media_ok = 0;
                atapi_sectors = 0;
                return BLK_ERR_NO_MEDIA;
            }
            if (sense_key == 0x06 && atapi_refresh_media() == BLK_OK)
                rc = atapi_packet_raw(packet, dst + n * 2048, 2048);
            if (rc != BLK_OK) return rc;
        }
    }
    return BLK_OK;
}

int atapi_read_sectors(unsigned lba, unsigned count, void *buf)
{
    unsigned char *dst = (unsigned char *)buf;
    if (!atapi_ok) return BLK_ERR_NODEV;
    if (!media_ok) return BLK_ERR_NO_MEDIA;
    if (!buf || !count) return BLK_ERR_IO;
    if (lba >= atapi_sectors * 4u || count > atapi_sectors * 4u - lba)
        return BLK_ERR_RANGE;
    if ((lba & 3u) == 0 && (count & 3u) == 0)
        return atapi_read_sectors_2048(lba / 4, count / 4, buf);

    for (unsigned i = 0; i < count; i++) {
        unsigned logical = lba + i;
        if (logical / 4 != window_lba) {
            int rc = atapi_read_sectors_2048(logical / 4, 1, sector_window);
            if (rc != BLK_OK) return rc;
            window_lba = logical / 4;
        }
        for (unsigned k = 0; k < 512; k++)
            dst[i * 512 + k] = sector_window[(logical & 3u) * 512 + k];
    }
    return BLK_OK;
}

int atapi_write_sectors(unsigned lba, unsigned count, const void *buf)
{
    (void)lba; (void)count; (void)buf;
    return BLK_ERR_READ_ONLY;
}
