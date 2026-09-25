#include "st7305_frame.h"

#define ROW_BYTES    (ST7305_WIDTH / 8)
#define BLOCKS       (ST7305_HEIGHT / 4) /* panel bytes per column pair */

static inline int is_black(const uint8_t *canonical, int x, int y)
{
    return (canonical[y * ROW_BYTES + (x >> 3)] >> (7 - (x & 7))) & 1;
}

void st7305_frame_to_panel(const uint8_t *canonical, uint8_t *panel)
{
    for (int pair = 0; pair < ST7305_WIDTH / 2; pair++) {
        for (int block = 0; block < BLOCKS; block++) {
            uint8_t byte = 0;
            for (int ly = 0; ly < 4; ly++) {
                int y = ST7305_HEIGHT - 1 - (block * 4 + ly); /* the panel runs bottom-up */
                for (int lx = 0; lx < 2; lx++) {
                    if (!is_black(canonical, pair * 2 + lx, y)) {
                        byte |= (uint8_t)(0x80u >> (ly * 2 + lx));
                    }
                }
            }
            panel[pair * BLOCKS + block] = byte;
        }
    }
}
