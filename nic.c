/* Minimal polled RTL8139 raw Ethernet driver for QEMU and PCI RTL8139 cards. */
#include "common.h"

#define RTL_RX_BYTES 8192u
#define RTL_TX_BYTES 2048u
#define RTL_RX_MASK  (RTL_RX_BYTES - 1)

static unsigned short io_base;
static unsigned char mac_address[6];
static unsigned char *rx_ring;
static unsigned char *tx_buffer[4];
static unsigned short rx_cursor;
static unsigned tx_cursor;
static int rx_count;
static int tx_count;
static int ready;
static volatile int nic_busy;

static unsigned short cfg_read16(unsigned dev, unsigned fn, unsigned reg)
{
    unsigned address = 0x80000000u | (dev << 11) | (fn << 8) | (reg & 0xFC);
    io_out32(0xCF8, address);
    return (unsigned short)(io_in32(0xCFC) >> ((reg & 2) * 8));
}

static void cfg_write16(unsigned dev, unsigned fn, unsigned reg, unsigned short value)
{
    unsigned address = 0x80000000u | (dev << 11) | (fn << 8) | (reg & 0xFC);
    unsigned shift = (reg & 2) * 8;
    unsigned old;
    io_out32(0xCF8, address);
    old = io_in32(0xCFC);
    old = (old & ~(0xFFFFu << shift)) | ((unsigned)value << shift);
    io_out32(0xCF8, address);
    io_out32(0xCFC, old);
}

static void *aligned_alloc(unsigned bytes, unsigned alignment)
{
    unsigned char *raw = (unsigned char *)mem_alloc(bytes + alignment - 1);
    unsigned address;
    if (!raw) return 0;
    address = ((unsigned)raw + alignment - 1) & ~(alignment - 1);
    return (void *)address;
}

int nic_init(void)
{
    unsigned dev, fn;
    unsigned vendor, device;
    unsigned bar;
    unsigned command;

    ready = 0;
    for (dev = 0; dev < 32; dev++) {
        for (fn = 0; fn < 8; fn++) {
            vendor = cfg_read16(dev, fn, 0);
            device = cfg_read16(dev, fn, 2);
            if (vendor == 0xFFFF) continue;
            if (!((vendor == 0x10EC && device == 0x8139) || device == 0x8139)) continue;

            io_out32(0xCF8, 0x80000000u | (dev << 11) | (fn << 8) | 0x10);
            bar = io_in32(0xCFC);
            if (!(bar & 1)) return -1;
            io_base = (unsigned short)(bar & 0xFFFC);
            cfg_write16(dev, fn, 4, cfg_read16(dev, fn, 4) | 0x0005);
            command = 0x10;
            for (unsigned wait = 0; wait < 1000000; wait++) {
                if (io_in8(io_base + 0x37) & 0x10) { command = 0; break; }
            }
            if (command) return -1;

            rx_ring = (unsigned char *)aligned_alloc(RTL_RX_BYTES + 16, 256);
            if (!rx_ring) return -1;
            for (unsigned i = 0; i < 4; i++) {
                tx_buffer[i] = (unsigned char *)aligned_alloc(RTL_TX_BYTES, 256);
                if (!tx_buffer[i]) return -1;
            }
            for (unsigned i = 0; i < RTL_RX_BYTES + 16; i++) rx_ring[i] = 0;
            for (unsigned i = 0; i < 6; i++) mac_address[i] = io_in8(io_base + i);

            io_out8(io_base + 0x3C, 0); /* polling mode */
            io_out32(io_base + 0x30, (unsigned)rx_ring);
            io_out16(io_base + 0x38, 0xFFF0);
            io_out32(io_base + 0x44, 0x0000008F); /* accept all, wrap ring */
            io_out32(io_base + 0x40, 0x03000700);
            io_out8(io_base + 0x37, 0x0C); /* receiver + transmitter */
            rx_cursor = tx_cursor = 0;
            rx_count = tx_count = 0;
            ready = 1;
            return 0;
        }
    }
    return -1;
}

int nic_present(void) { return ready; }

int nic_mac(unsigned char out[6])
{
    if (!ready || !out) return -1;
    for (unsigned i = 0; i < 6; i++) out[i] = mac_address[i];
    return 0;
}

int nic_send(const void *frame, unsigned length)
{
    const unsigned char *src = (const unsigned char *)frame;
    unsigned char *dst;
    unsigned port;
    unsigned i;
    int result = -1;
    if (!ready || !src || length < 14 || length > 1518) return -1;
    if (__sync_lock_test_and_set(&nic_busy, 1)) return -1;

    port = tx_cursor & 3;
    dst = tx_buffer[port];
    for (i = 0; i < length; i++) dst[i] = src[i];
    for (; i < 60; i++) dst[i] = 0;
    for (i = 0; i < 1000000; i++)
        if (!(io_in32(io_base + 0x10 + port * 4) & 0x2000)) break;
    if (i < 1000000) {
        io_out32(io_base + 0x20 + port * 4, (unsigned)dst);
        io_out32(io_base + 0x10 + port * 4, length < 60 ? 60 : length);
        for (i = 0; i < 4000000; i++) {
            unsigned status = io_in32(io_base + 0x10 + port * 4);
            if (status & 0x8000) { result = (status & 0x4000) ? -1 : 0; break; }
        }
        if (result == 0) { tx_cursor++; tx_count++; }
    }
    __sync_lock_release(&nic_busy);
    return result;
}

static unsigned char ring_byte(unsigned index)
{
    return rx_ring[index & RTL_RX_MASK];
}

int nic_receive(void *frame, unsigned capacity)
{
    unsigned char *dst = (unsigned char *)frame;
    unsigned status, length, copied, i, next;
    if (!ready || !dst || capacity < 14) return -1;
    if (__sync_lock_test_and_set(&nic_busy, 1)) return -1;
    if (io_in8(io_base + 0x37) & 1) {
        __sync_lock_release(&nic_busy);
        return 0;
    }
    status = ring_byte(rx_cursor) | ((unsigned)ring_byte(rx_cursor + 1) << 8);
    length = ring_byte(rx_cursor + 2) | ((unsigned)ring_byte(rx_cursor + 3) << 8);
    next = (rx_cursor + 4 + length + 3) & ~3u;
    copied = length >= 4 ? length - 4 : 0;
    if (length < 18 || length > 1536 || !(status & 1) || !copied) {
        rx_cursor = (unsigned short)(next & RTL_RX_MASK);
        io_out16(io_base + 0x38, (unsigned short)(rx_cursor - 16));
        __sync_lock_release(&nic_busy);
        return -1;
    }
    if (copied > capacity) copied = capacity;
    for (i = 0; i < copied; i++) dst[i] = ring_byte(rx_cursor + 4 + i);
    rx_cursor = (unsigned short)(next & RTL_RX_MASK);
    io_out16(io_base + 0x38, (unsigned short)(rx_cursor - 16));
    rx_count++;
    __sync_lock_release(&nic_busy);
    return (int)copied;
}

int nic_received_count(void) { return rx_count; }
int nic_transmitted_count(void) { return tx_count; }
