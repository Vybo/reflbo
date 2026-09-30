#include "gfx.h"
#include "qrcodegen.h"

/* QR codes (spec §4.3) through Nayuki's qrcodegen, vendored in qrcodegen/ (MIT). */

#define QR_MAX_VERSION 10 /* 57 modules: a Wi-Fi join string or a URL needs version 2-4 */
#define QUIET 4           /* modules of white border, as ISO/IEC 18004 asks */

static uint8_t s_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
static uint8_t s_temp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];

static int encode(const char *text)
{
    if (text == NULL || !qrcodegen_encodeText(text, s_temp, s_qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                              QR_MAX_VERSION, qrcodegen_Mask_AUTO, true)) {
        return 0;
    }
    return qrcodegen_getSize(s_qr);
}

int gfx_qr_side(const char *text, int scale)
{
    int modules = encode(text);
    return modules ? (modules + 2 * QUIET) * scale : 0;
}

int gfx_qr(gfx_fb_t *fb, int x, int y, int scale, const char *text)
{
    int modules = encode(text);
    if (modules == 0 || scale < 1) {
        return 0;
    }
    int side = (modules + 2 * QUIET) * scale;
    gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)side, (int16_t)side }, GFX_WHITE);
    for (int my = 0; my < modules; my++) {
        for (int mx = 0; mx < modules; mx++) {
            if (qrcodegen_getModule(s_qr, mx, my)) {
                gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x + (QUIET + mx) * scale),
                                                (int16_t)(y + (QUIET + my) * scale), (int16_t)scale,
                                                (int16_t)scale },
                              GFX_BLACK);
            }
        }
    }
    return side;
}
