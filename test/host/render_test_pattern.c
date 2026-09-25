#include <stdio.h>

#include "gfx.h"
#include "gfx_test_pattern.h"

/* Writes the test pattern as PBM: render_test_pattern OUT.pbm */
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUT.pbm\n", argv[0]);
        return 2;
    }
    static uint8_t buf[400 * 300 / 8];
    static uint8_t pbm[16000];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    gfx_draw_test_pattern(&fb);
    size_t n = gfx_pbm_encode(&fb, pbm, sizeof(pbm));
    FILE *f = fopen(argv[1], "wb");
    if (f == NULL || fwrite(pbm, 1, n, f) != n) {
        perror(argv[1]);
        return 1;
    }
    fclose(f);
    return 0;
}
