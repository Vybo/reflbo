#pragma once

#include <stddef.h>

#include "ui_fields.h"

/* What the web UI's preset editor needs to know (spec §10.3), as JSON. Pure C, host-buildable. */

/* GET /api/layouts: the panel size and every layout's slots, with their rectangles, size classes
 * and the field kinds each one takes. Returns the length written, or 0 if `size` is too small. */
size_t ui_catalog_layouts_json(char *out, size_t size);
/* GET /api/fields: every field with its kind, its label in English (the web UI's language, spec
 * §5.8) and its value right now in the device's language. 0 if `size` is too small. */
size_t ui_catalog_fields_json(const ui_context_t *ctx, char *out, size_t size);
