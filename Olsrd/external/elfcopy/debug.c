#include <debug.h>
#include <stdio.h>
#include <ctype.h>

#define NUM_COLS  (32)

/* returns the number of non-zero non-printable characters. */

int dump_hex_buffer(FILE *s, void *b, size_t len, size_t elsize) {
    int num_nonprintable = 0;
    int i, last;
    char *pchr = (char *)b;
    fputc('\n', s);
    for (i = last = 0; i < len; i++) {
        if (!elsize) {
            if (i && !(i % 4)) fprintf(s, " ");
            if (i && !(i % 8)) fprintf(s, " ");
        } else {
            if (i && !(i % elsize)) fprintf(s, " ");
        }

        if (i && !(i % NUM_COLS)) {
            while (last < i) {
                if (isprint(pchr[last]))
                    fputc(pchr[last], s);
                else {
                    fputc('.', s);
                    if(pchr[last])
                        num_nonprintable++;
                }
                last++;
            }
            fprintf(s, " (%d)\n", i);
        }
        fprintf(s, "%02x", (unsigned char)pchr[i]);
    }
    if (i && (i % NUM_COLS)) fputs("\n", s);
    return num_nonprintable;
}
