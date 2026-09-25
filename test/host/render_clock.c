#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "ui_fixtures.h"

/* Writes a clock-screen fixture as PBM: render_clock <valid|invalid|cold> OUT.pbm */
int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <valid|invalid|cold> OUT.pbm\n", argv[0]);
        return 2;
    }
    ui_clock_t clock;
    if (strcmp(argv[1], "valid") == 0) {
        clock = fixture_clock_valid();
    } else if (strcmp(argv[1], "invalid") == 0) {
        clock = fixture_clock_invalid();
    } else if (strcmp(argv[1], "cold") == 0) {
        clock = fixture_clock_cold();
    } else {
        fprintf(stderr, "unknown fixture %s\n", argv[1]);
        return 2;
    }
    static uint8_t buf[400 * 300 / 8];
    static uint8_t pbm[16000];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    ui_draw_clock(&fb, &clock);
    size_t n = gfx_pbm_encode(&fb, pbm, sizeof(pbm));
    FILE *f = fopen(argv[2], "wb");
    if (f == NULL || fwrite(pbm, 1, n, f) != n) {
        perror(argv[2]);
        return 1;
    }
    fclose(f);
    return 0;
}
