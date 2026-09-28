#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"

/* build-host/render_dashboard <fixture> <out.pbm>: one dashboard fixture as PBM (tools/render.py). */
int main(int argc, char **argv)
{
    static uint8_t buf[400 * 300 / 8];
    static uint8_t pbm[16000];
    ui_context_t ctx;
    ui_preset_t preset;
    if (argc != 3 || !fixture_dashboard(argv[1], &ctx, &preset)) {
        fprintf(stderr, "usage: render_dashboard <fixture> <out.pbm>\n");
        return 2;
    }
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    ui_draw_dashboard(&fb, &ctx, &preset);
    size_t n = gfx_pbm_encode(&fb, pbm, sizeof(pbm));
    FILE *f = fopen(argv[2], "wb");
    if (f == NULL || fwrite(pbm, 1, n, f) != n) {
        return 1;
    }
    fclose(f);
    return 0;
}
