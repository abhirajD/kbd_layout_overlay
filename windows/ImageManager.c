#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ImageManager.h"
#include "../shared/overlay.h"
#include "../shared/config.h"
#include "../shared/log.h"

static Config *g_config = NULL;
static Overlay g_overlay;
static unsigned char *g_original_image = NULL;
static int g_original_image_size = 0;
static float g_last_scale = -1.0f;
static int g_last_custom_width = -1;
static int g_last_custom_height = -1;
static int g_last_use_custom = -1;

static int ensure_original_image(void) {
    if (g_original_image) return 1;

    const char *search_paths[] = {
        "keymap.png",
        "assets\\keymap.png",
        "..\\assets\\keymap.png",
        NULL
    };

    for (int i = 0; search_paths[i]; i++) {
        FILE *f = fopen(search_paths[i], "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0) {
                g_original_image = (unsigned char *)malloc((size_t)sz);
                if (!g_original_image) {
                    fclose(f);
                    return 0;
                }
                if (fread(g_original_image, 1, (size_t)sz, f) == (size_t)sz) {
                    g_original_image_size = (int)sz;
                    fclose(f);
                    return 1;
                }
                free(g_original_image);
                g_original_image = NULL;
            }
            fclose(f);
        }
    }

    int size = 0;
    const unsigned char *data = get_default_keymap(&size);
    if (data && size > 0) {
        g_original_image = (unsigned char *)malloc((size_t)size);
        if (!g_original_image) return 0;
        memcpy(g_original_image, data, (size_t)size);
        g_original_image_size = size;
        return 1;
    }

    return 0;
}

int image_manager_init(Config *config) {
    g_config = config;
    memset(&g_overlay, 0, sizeof(g_overlay));
    return 1;
}

void image_manager_cleanup(void) {
    free_overlay(&g_overlay);
    if (g_original_image) {
        free(g_original_image);
        g_original_image = NULL;
        g_original_image_size = 0;
    }
}

int image_manager_load_overlay(void) {
    if (!ensure_original_image()) {
        logger_log("Could not find keymap.png or embedded fallback");
        return 0;
    }

    /* Determine overlay dimensions */
    int max_w, max_h;
    /* Get active monitor dimensions */
    RECT mon = {0};
    mon.right = GetSystemMetrics(SM_CXSCREEN);
    mon.bottom = GetSystemMetrics(SM_CYSCREEN);

    int screen_w = mon.right - mon.left;
    int screen_h = mon.bottom - mon.top;
    if (g_config->use_custom_size) {
        /* Use custom pixel dimensions */
        max_w = g_config->custom_width_px;
        max_h = g_config->custom_height_px;
    } else {
        /* Apply configurable scaling to monitor dimensions */
        max_w = (int)(screen_w * g_config->scale);
        max_h = (int)(screen_h * g_config->scale);
    }

    OverlayError result = load_overlay_mem(g_original_image, g_original_image_size,
                                           max_w, max_h, &g_overlay);
    if (result != OVERLAY_OK) {
        const char *error_msg = "Unknown error";
        switch (result) {
            case OVERLAY_ERROR_FILE_NOT_FOUND:
                error_msg = "Could not find keymap.png in any location"; break;
            case OVERLAY_ERROR_DECODE_FAILED:
                error_msg = "Could not decode image file"; break;
            case OVERLAY_ERROR_OUT_OF_MEMORY:
                error_msg = "Out of memory loading image"; break;
            case OVERLAY_ERROR_RESIZE_FAILED:
                error_msg = "Failed to resize image"; break;
            case OVERLAY_ERROR_NULL_PARAM:
                error_msg = "Internal error"; break;
        }

        logger_log("Overlay loading failed: %s", error_msg);
        return 0;
    }

    apply_effects(&g_overlay, g_config->opacity, g_config->invert);
    g_last_scale = g_config->scale;
    g_last_custom_width = g_config->custom_width_px;
    g_last_custom_height = g_config->custom_height_px;
    g_last_use_custom = g_config->use_custom_size;

    logger_log("Overlay loaded: %dx%d", g_overlay.width, g_overlay.height);
    return 1;
}

int image_manager_reload_if_needed(void) {
    if (fabsf(g_config->scale - g_last_scale) < 0.001f &&
        g_config->custom_width_px == g_last_custom_width &&
        g_config->custom_height_px == g_last_custom_height &&
        g_config->use_custom_size == g_last_use_custom) {
        return 0; /* No changes */
    }

    g_last_scale = g_config->scale;
    g_last_custom_width = g_config->custom_width_px;
    g_last_custom_height = g_config->custom_height_px;
    g_last_use_custom = g_config->use_custom_size;

    free_overlay(&g_overlay);

    int max_w, max_h;
    if (g_config->use_custom_size) {
        max_w = g_config->custom_width_px;
        max_h = g_config->custom_height_px;
    } else {
        max_w = (int)(1920 * g_config->scale);
        max_h = (int)(1080 * g_config->scale);
    }

    OverlayError res = load_overlay_mem(g_original_image, g_original_image_size,
                                        max_w, max_h, &g_overlay);
    if (res == OVERLAY_OK) {
        apply_effects(&g_overlay, g_config->opacity, g_config->invert);
        logger_log("Overlay reloaded: %dx%d", g_overlay.width, g_overlay.height);
        return 1;
    }

    logger_log("Overlay reload failed");
    return 0;
}

void image_manager_apply_effects(void) {
    apply_effects(&g_overlay, g_config->opacity, g_config->invert);
}

Overlay* image_manager_get_overlay(void) {
    return &g_overlay;
}

int image_manager_get_dimensions(int *width, int *height) {
    if (width) *width = g_overlay.width;
    if (height) *height = g_overlay.height;
    return 1;
}
