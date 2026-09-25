#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag_internal.h"
#include "display.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "gfx_test_pattern.h"
#include "util_base64.h"

#define PBM_CHUNK 57 /* bytes per line: 76 base64 characters */

static int cmd_screenshot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        printf("screenshot: display not initialised\n");
        return 1;
    }
    size_t size = gfx_pbm_size(fb);
    uint8_t *pbm = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (pbm == NULL) {
        printf("screenshot: out of memory\n");
        return 1;
    }
    size_t len = gfx_pbm_encode(fb, pbm, size);
    char line[80];
    printf("-----BEGIN RLCD PBM-----\n");
    for (size_t off = 0; off < len; off += PBM_CHUNK) {
        size_t chunk = len - off < PBM_CHUNK ? len - off : PBM_CHUNK;
        util_base64_encode(pbm + off, chunk, line, sizeof(line));
        printf("%s\n", line);
    }
    printf("-----END RLCD PBM-----\n");
    free(pbm);
    return 0;
}

static int print_status(void)
{
    printf("panel %s, %s\n", st7305_variant() == ST7305_VARIANT_XIAOZHI ? "xiaozhi" : "factory",
           st7305_mode() == ST7305_MODE_LPM ? "lpm" : "hpm");
    return 0;
}

static int cmd_panel(int argc, char **argv)
{
    esp_err_t err = ESP_ERR_INVALID_ARG;
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        printf("panel: display not initialised\n");
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        return print_status();
    } else if (argc == 2 && strcmp(argv[1], "test") == 0) {
        gfx_draw_test_pattern(fb);
        err = display_commit(false);
    } else if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        gfx_clear(fb, GFX_WHITE);
        err = display_commit(false);
    } else if (argc == 3 && strcmp(argv[1], "mode") == 0 && strcmp(argv[2], "hpm") == 0) {
        err = st7305_set_mode(ST7305_MODE_HPM);
    } else if (argc == 3 && strcmp(argv[1], "mode") == 0 && strcmp(argv[2], "lpm") == 0) {
        err = st7305_set_mode(ST7305_MODE_LPM);
    } else if (argc == 3 && strcmp(argv[1], "init") == 0 && strcmp(argv[2], "factory") == 0) {
        err = display_set_variant(ST7305_VARIANT_FACTORY);
    } else if (argc == 3 && strcmp(argv[1], "init") == 0 && strcmp(argv[2], "xiaozhi") == 0) {
        err = display_set_variant(ST7305_VARIANT_XIAOZHI);
    } else {
        printf("usage: panel status | test | clear | mode <hpm|lpm> | init <factory|xiaozhi>\n");
        return 1;
    }
    if (err != ESP_OK) {
        printf("panel: %s\n", esp_err_to_name(err));
        return 1;
    }
    return print_status();
}

esp_err_t diag_register_display_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "screenshot", .help = "Print the framebuffer as base64 PBM between markers", .func = &cmd_screenshot },
        { .command = "panel", .help = "panel status | test | clear | mode <hpm|lpm> | init <factory|xiaozhi>",
          .func = &cmd_panel },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
