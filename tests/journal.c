#include "vkmin.h"
#include <stdio.h>
#include <string.h>

/* Drive the real parser with truncated/oversized records and illegal
 * relocations. The valid file is used only to initialize replay dimensions. */
int main(int argc, char **argv) {
    if (argc != 3) return 1;
    unsigned char header[32];
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    const size_t got = fread(header, 1, sizeof header, f);
    fclose(f);
    if (got != sizeof header) return 3;
    char replay_flag[] = "--replay";
    char *args[] = {argv[0], replay_flag, argv[1]};
    vkmin_ctx *c = vkmin_init(&(vkmin_desc){.argc = 3, .argv = args});
    for (unsigned test = 0; test < 8; ++test) {
        uint32_t record[8] = {1, 0, 8, 1, 0, 0, UINT32_MAX, 1};
        size_t bytes = sizeof record, head = sizeof header;
        switch (test) {
        case 0: head = 7; bytes = 0; break;
        case 1: bytes = 3; break;
        case 2: record[0] = UINT32_MAX; break;
        case 3: record[1] = 257; break;
        case 4: record[2] = UINT32_MAX; break;
        case 5: record[3] = UINT32_MAX; break;
        case 6: break; /* offset beyond payload */
        case 7: record[6] = 0; record[7] = 99; break;
        }
        f = fopen(argv[2], "wb");
        if (!f) return 4;
        const bool written = fwrite(header, 1, head, f) == head && fwrite(record, 1, bytes, f) == bytes;
        if (fclose(f) != 0 || !written) return 5;
        if (vkmin_replay(c, argv[2])) { fprintf(stderr, "journal: accepted corrupt case %u\n", test); return 6; }
    }
    vkmin_shutdown(c);
    puts("journal: ok (eight corrupt files rejected)");
    return 0;
}
