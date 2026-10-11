/* HEX -- small binary viewer and ELF inspector for AMUNOS. */
#include <stdio.h>
#include <string.h>

typedef struct __attribute__((packed)) {
    unsigned char ident[16];
    unsigned short type, machine;
    unsigned int version, entry, phoff, shoff, flags;
    unsigned short ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf32_header_t;

typedef struct __attribute__((packed)) {
    unsigned int type, offset, vaddr, paddr, filesz, memsz, flags, align;
} elf32_phdr_t;

static const char *elf_type(unsigned type)
{
    if (type == 1) return "RELOCATABLE";
    if (type == 2) return "EXECUTABLE";
    if (type == 3) return "SHARED";
    return "OTHER";
}

static void print_ascii(const unsigned char *p, int n)
{
    int i;
    for (i = 0; i < 16; i++)
        putchar(i < n && p[i] >= 32 && p[i] <= 126 ? p[i] : ' ');
}

static void dump_bytes(const unsigned char *p, int n)
{
    int base, i;
    for (base = 0; base < n; base += 16) {
        printf("%08X  ", base);
        for (i = 0; i < 16; i++) {
            if (base + i < n) printf("%02X ", p[base + i]);
            else printf("   ");
        }
        printf(" | ");
        print_ascii(p + base, n - base);
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    FILE *fp;
    unsigned char sample[256];
    elf32_header_t eh;
    elf32_phdr_t ph;
    long size;
    int n, i;

    if (argc < 2) {
        printf("AMUNOS HEX 1.0 -- BINARY / ELF INSPECTOR\n");
        printf("USAGE: HEX FILE\n");
        printf("SHOWS THE FIRST 256 BYTES AND ELF32 PROGRAM HEADERS.\n");
        return 1;
    }
    fp = fopen(argv[1], "rb");
    if (!fp) { printf("HEX: CANNOT OPEN %s\n", argv[1]); return 1; }
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp); printf("HEX: CANNOT READ FILE SIZE\n"); return 1;
    }
    n = (int)fread(sample, 1, sizeof(sample), fp);
    printf("AMUNOS HEX 1.0\nFILE: %s\nSIZE: %ld BYTES\n\n", argv[1], size);
    dump_bytes(sample, n);

    if (n < (int)sizeof(eh) || sample[0] != 0x7f || sample[1] != 'E' ||
        sample[2] != 'L' || sample[3] != 'F') {
        printf("\nFORMAT: NON-ELF BINARY\n");
        fclose(fp);
        return 0;
    }
    memcpy(&eh, sample, sizeof(eh));
    if (eh.ident[4] != 1 || eh.ident[5] != 1) {
        printf("\nELF: UNSUPPORTED (EXPECTS ELF32 LITTLE-ENDIAN)\n");
        fclose(fp);
        return 0;
    }
    printf("\nELF32 %s  MACHINE: %u  ENTRY: %08X\n",
           elf_type(eh.type), eh.machine, eh.entry);
    printf("PROGRAM HEADERS: %u  OFFSET: %08X\n", eh.phnum, eh.phoff);
    if (eh.phentsize < sizeof(ph) || eh.phnum > 16) {
        printf("ELF: UNSUPPORTED PROGRAM HEADER TABLE\n");
        fclose(fp);
        return 0;
    }
    for (i = 0; i < eh.phnum; i++) {
        if (fseek(fp, (long)eh.phoff + (long)i * eh.phentsize, SEEK_SET) != 0 ||
            fread(&ph, 1, sizeof(ph), fp) != sizeof(ph)) {
            printf("HEADER %d: READ ERROR\n", i);
            break;
        }
        printf("PH%u TYPE=%u OFF=%08X VADDR=%08X FILE=%u MEM=%u FLAGS=%X\n",
               i, ph.type, ph.offset, ph.vaddr, ph.filesz, ph.memsz, ph.flags);
    }
    fclose(fp);
    return 0;
}
