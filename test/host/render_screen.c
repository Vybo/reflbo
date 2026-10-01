#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "screen_fixtures.h"

/* build-host/render_screen <fixture> <out.pbm>: one menu or special-screen fixture as PBM
 * (tools/render.py). `--list` prints the fixture names. */
int main(int argc, char **argv)
{
    static uint8_t buf[400 * 300 / 8];
    static uint8_t pbm[16000];
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (size_t i = 0; i < sizeof(k_screen_fixtures) / sizeof(k_screen_fixtures[0]); i++) {
            printf("%s\n", k_screen_fixtures[i]);
        }
        return 0;
    }
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    if (argc != 3 || !fixture_screen(argv[1], &fb)) {
        fprintf(stderr, "usage: render_screen <fixture> <out.pbm> | --list\n");
        return 2;
    }
    size_t n = gfx_pbm_encode(&fb, pbm, sizeof(pbm));
    FILE *f = fopen(argv[2], "wb");
    if (f == NULL || fwrite(pbm, 1, n, f) != n) {
        return 1;
    }
    fclose(f);
    return 0;
}
