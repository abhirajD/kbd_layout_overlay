#ifdef _WIN32
#include <windows.h>
#endif
#include "app_context.h"
#include <stdlib.h>
#include <string.h>

klo_error_t klo_context_init(klo_app_context_t *ctx, const char *config_path) {
    KLO_CHECK_PARAM(ctx);
    KLO_CHECK_PARAM(config_path);
    
    /* Initialize all fields to zero/NULL */
    memset(ctx, 0, sizeof(klo_app_context_t));
    
    /* Set up configuration path */
    size_t path_len = strlen(config_path);
    ctx->config_path = malloc(path_len + 1);
    KLO_CHECK_MEMORY(ctx->config_path);
    strcpy(ctx->config_path, config_path);
    
    /* Initialize configuration with defaults */
    klo_error_t err = init_config(&ctx->config);
    if (err != KLO_OK) {
        free(ctx->config_path);
        ctx->config_path = NULL;
        return err;
    }
    
    /* Initialize sub-contexts */
    err = klo_graphics_init(&ctx->graphics);
    if (err != KLO_OK) {
        free_config(&ctx->config);
        free(ctx->config_path);
        ctx->config_path = NULL;
        return err;
    }
    
    err = klo_ui_init(&ctx->ui);
    if (err != KLO_OK) {
        klo_graphics_cleanup(&ctx->graphics);
        free_config(&ctx->config);
        free(ctx->config_path);
        ctx->config_path = NULL;
        return err;
    }
    
    ctx->is_initialized = 1;
    return KLO_OK;
}

void klo_context_free(klo_app_context_t *ctx) {
    if (!ctx || !ctx->is_initialized) {
        return;
    }
    
    /* Cleanup in reverse order of initialization */
    klo_hotkey_cleanup(&ctx->hotkey);
    klo_ui_cleanup(&ctx->ui);
    klo_graphics_cleanup(&ctx->graphics);
    
    /* Free overlay resources */
    free_overlay_cache(&ctx->overlay_cache);
    free_overlay(&ctx->overlay);
    
    /* Free configuration */
    free_config(&ctx->config);
    free(ctx->config_path);
    
    /* Clear the context */
    memset(ctx, 0, sizeof(klo_app_context_t));
}

klo_error_t klo_context_load_config(klo_app_context_t *ctx) {
    KLO_CHECK_PARAM(ctx);
    KLO_CHECK_PARAM(ctx->config_path);
    
    if (!ctx->is_initialized) {
        KLO_ERROR_RETURN(KLO_ERR_NOT_INITIALIZED, "Context not initialized");
    }
    
    return load_config(ctx->config_path, &ctx->config);
}

klo_error_t klo_context_save_config(klo_app_context_t *ctx) {
    KLO_CHECK_PARAM(ctx);
    KLO_CHECK_PARAM(ctx->config_path);
    
    if (!ctx->is_initialized) {
        KLO_ERROR_RETURN(KLO_ERR_NOT_INITIALIZED, "Context not initialized");
    }
    
    return save_config(ctx->config_path, &ctx->config);
}

klo_error_t klo_context_reload_overlay(klo_app_context_t *ctx) {
    KLO_CHECK_PARAM(ctx);
    
    if (!ctx->is_initialized) {
        KLO_ERROR_RETURN(KLO_ERR_NOT_INITIALIZED, "Context not initialized");
    }
    
    /* Free existing overlay data */
    free_overlay_cache(&ctx->overlay_cache);
    free_overlay(&ctx->overlay);
    
    /* Determine overlay path */
    const char *path = (ctx->config.overlay_path && ctx->config.overlay_path[0]) 
                      ? ctx->config.overlay_path 
                      : "keymap.png";
    
    /* Load new overlay - dimensions will be determined by monitor setup */
    int max_width = 1920;  /* Default fallback */
    int max_height = 1080;
    
    int result = load_overlay_image(path, max_width, max_height, &ctx->overlay);
    if (result != OVERLAY_OK) {
        if (result == OVERLAY_ERR_NOT_FOUND) {
            KLO_ERROR_RETURN(KLO_ERR_OVERLAY_NOT_FOUND, "Overlay image file not found");
        } else if (result == OVERLAY_ERR_DECODE) {
            KLO_ERROR_RETURN(KLO_ERR_OVERLAY_DECODE, "Failed to decode overlay image");
        } else {
            KLO_ERROR_RETURN(KLO_ERR_OVERLAY_MEMORY, "Memory allocation failed for overlay");
        }
    }
    
    /* Initialize cache */
    result = init_overlay_cache(&ctx->overlay_cache, &ctx->overlay);
    if (result != OVERLAY_OK) {
        free_overlay(&ctx->overlay);
        KLO_ERROR_RETURN(KLO_ERR_OVERLAY_MEMORY, "Failed to initialize overlay cache");
    }
    
    return KLO_OK;
}

#ifndef _WIN32
/* Graphics context implementation - platform-agnostic base */
klo_error_t klo_graphics_init(klo_graphics_context_t *gfx) {
    KLO_CHECK_PARAM(gfx);
    
    memset(gfx, 0, sizeof(klo_graphics_context_t));
    
    /* Platform-specific initialization will be done in platform code */
    return KLO_OK;
}

void klo_graphics_cleanup(klo_graphics_context_t *gfx) {
    if (!gfx) return;
    
    /* Platform-specific cleanup will be done in platform code */
    memset(gfx, 0, sizeof(klo_graphics_context_t));
}

klo_error_t klo_graphics_update_bitmap(klo_graphics_context_t *gfx, const Overlay *overlay) {
    KLO_CHECK_PARAM(gfx);
    KLO_CHECK_PARAM(overlay);
    
    /* This will be implemented by platform-specific code */
    gfx->bitmap_width = overlay->width;
    gfx->bitmap_height = overlay->height;
    
    return KLO_OK;
}
#endif

/* Hotkey context implementation - platform-agnostic base */
klo_error_t klo_hotkey_init(klo_hotkey_context_t *hk, const char *hotkey_string) {
    KLO_CHECK_PARAM(hk);
    
    memset(hk, 0, sizeof(klo_hotkey_context_t));
    
    if (hotkey_string) {
        /* Platform-specific parsing will be done in platform code */
        klo_log(KLO_LOG_DEBUG, "Initializing hotkey: %s", hotkey_string);
    }
    
    return KLO_OK;
}

void klo_hotkey_cleanup(klo_hotkey_context_t *hk) {
    if (!hk) return;
    
    /* Ensure hotkey is unregistered */
    if (hk->is_registered) {
        klo_hotkey_unregister(hk);
    }
    
    memset(hk, 0, sizeof(klo_hotkey_context_t));
}

#ifndef _WIN32
klo_error_t klo_hotkey_register(klo_hotkey_context_t *hk, klo_window_handle_t window) {
    KLO_CHECK_PARAM(hk);
    
    /* Platform-specific implementation (non-Windows default) */
    hk->is_registered = 1;
    return KLO_OK;
}

klo_error_t klo_hotkey_unregister(klo_hotkey_context_t *hk) {
    KLO_CHECK_PARAM(hk);
    
    /* Platform-specific implementation (non-Windows default) */
    hk->is_registered = 0;
    return KLO_OK;
}
#endif

#ifndef _WIN32
/* UI context implementation - platform-agnostic base */
klo_error_t klo_ui_init(klo_ui_context_t *ui) {
    KLO_CHECK_PARAM(ui);
    
    memset(ui, 0, sizeof(klo_ui_context_t));
    return KLO_OK;
}

void klo_ui_cleanup(klo_ui_context_t *ui) {
    if (!ui) return;
    
    /* Platform-specific cleanup will be done in platform code */
    memset(ui, 0, sizeof(klo_ui_context_t));
}

klo_error_t klo_ui_create_window(klo_ui_context_t *ui, int width, int height) {
    KLO_CHECK_PARAM(ui);
    
    ui->width = width;
    ui->height = height;
    
    /* Platform-specific window creation */
    return KLO_OK;
}

klo_error_t klo_ui_show_window(klo_ui_context_t *ui, int show) {
    KLO_CHECK_PARAM(ui);
    
    ui->is_visible = show;
    
    /* Platform-specific window show/hide */
    return KLO_OK;
}

klo_error_t klo_ui_update_position(klo_ui_context_t *ui, int x, int y) {
    KLO_CHECK_PARAM(ui);
    
    ui->x = x;
    ui->y = y;
    
    /* Platform-specific position update */
    return KLO_OK;
}
#endif
