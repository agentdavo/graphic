/* bin2c -- embed a SPIR-V module as a C array so the binary has no file-path
 * failure mode and no runtime shader compiler. Usage: bin2c <in.spv> <symbol> */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: bin2c <in.spv> <symbol>\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "bin2c: cannot open %s\n", argv[1]);
        return 1;
    }
    printf("static const uint32_t %s[] = {\n", argv[2]);
    unsigned long words = 0;
    unsigned int word = 0;
    while (fread(&word, sizeof word, 1, f) == 1) {
        printf("%s0x%08xu,", (words % 6 == 0) ? "    " : " ", word);
        ++words;
        if (words % 6 == 0) printf("\n");
    }
    if (words % 6 != 0) printf("\n");
    printf("};\n\n");
    if (ferror(f)) {
        fclose(f);
        fprintf(stderr, "bin2c: read error on %s\n", argv[1]);
        return 1;
    }
    fclose(f);
    if (words == 0) {
        fprintf(stderr, "bin2c: %s is empty\n", argv[1]);
        return 1;
    }
    return 0;
}
