/* Host test: lay out a page produced by `html2ce.py --bin`, check the layout rules,
 * and print it in a form that preview.py can draw.
 *   gcc -Wall -Wextra -I../src test_core.c ../src/core.c -o test_core
 *   ./test_core PAGE.bin > layout.txt
 */
#include <stdio.h>

#include "core.h"

int main(int argc, char **argv)
{
    static uint8_t buf[70000];
    static Doc d;
    FILE *f;
    size_t n;
    int r, errors = 0;
    uint16_t i, expect_y = 0;
    LineView v;

    if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
        fprintf(stderr, "usage: %s PAGE.bin\n", argv[0]);
        return 2;
    }
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);

    r = doc_layout(&d, buf, (uint16_t)n);
    if (r != DOC_OK) {
        printf("ERR doc_layout failed: %d\n", r);
        return 1;
    }
    printf("DOC \"%s\" prev=\"%s\" next=\"%s\" lines=%u total_h=%u truncated=%d arena=%u/%u\n",
           d.title, d.prev, d.next, d.nlines, d.total_h, d.truncated, d.arena_used, ARENA_CAP);
    for (i = 0; i < d.nlines; i++) {
        doc_line(&d, i, &v);
        printf("L %u y=%u h=%u kind=%u center=%u indent=%u\n", i, v.y, v.height, v.kind,
               v.center, v.indent);
        if (v.y != expect_y) { printf("ERR line %u y not contiguous\n", i); errors++; }
        expect_y = (uint16_t)(v.y + v.height);
        if (v.kind == LK_TEXT) {
            const uint8_t *p = v.p;
            uint8_t nseg = *p++, s, k;
            unsigned width = 0;

            for (s = 0; s < nseg; s++) {
                uint8_t st = p[0], len = p[1];

                printf("S %u %u ", st, len);
                for (k = 0; k < len; k++)
                    putchar(p[2 + k]);
                putchar('\n');
                width += (unsigned)len * CHAR_W * head_sx(STYLE_HEAD(st));
                p += 2 + len;
            }
            if (width > (unsigned)(VIEW_W - v.indent)) {
                printf("ERR line %u is %u px wide (max %u)\n", i, width, VIEW_W - v.indent);
                errors++;
            }
        } else if (v.kind == LK_IMAGE) {
            printf("I %.8s %u %u\n", (const char *)v.p, v.p[8], v.p[9]);
        }
    }
    if (d.total_h != expect_y) { printf("ERR total_h mismatch\n"); errors++; }
    printf("END errors=%d\n", errors);
    return errors != 0;
}
