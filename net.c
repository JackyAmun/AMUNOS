#include <stdio.h>
#include <string.h>
#include <syscall.h>

static void print_mac(const unsigned char *mac)
{
    for (int i = 0; i < 6; i++) {
        if (i) printf(":");
        printf("%02x", mac[i]);
    }
}

int main(int argc, char **argv)
{
    unsigned char mac[6];
    unsigned char frame[1518];
    int length;
    if (sys_net_mac(mac) != 0) {
        printf("RTL8139 not found\n");
        return 1;
    }

    if (argc > 1 && strcmp(argv[1], "SEND") == 0) {
        unsigned n = argc > 2 ? (unsigned)strlen(argv[2]) : 0;
        if (!n || n > 1400) {
            printf("Usage: NET SEND text\n");
            return 1;
        }
        memset(frame, 0, 60);
        for (int i = 0; i < 6; i++) frame[i] = 0xFF;
        for (int i = 0; i < 6; i++) frame[6 + i] = mac[i];
        frame[12] = 0x88; frame[13] = 0xB5;
        memcpy(frame + 14, argv[2], n);
        if (sys_net_send(frame, n + 14) != 0) {
            printf("Transmit failed\n");
            return 1;
        }
        printf("Sent EtherType 88B5 frame from ");
        print_mac(mac);
        printf("\n");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "RX") == 0) {
        length = sys_net_receive(frame, sizeof(frame));
        if (length <= 0) {
            printf(length == 0 ? "No frame waiting\n" : "Receive failed\n");
            return length < 0;
        }
        printf("Frame %d bytes, source ", length);
        print_mac(frame + 6);
        printf(", EtherType %02x%02x\n", frame[12], frame[13]);
        return 0;
    }

    printf("RTL8139 raw Ethernet\nMAC: ");
    print_mac(mac);
    printf("\nNET SEND text | NET RX\n");
    return 0;
}
