#include <windows.h>
#include "../shared/app_context.h"
#include "../shared/error.h"
#include <string.h>

/* Minimal Windows platform adapters for shared context.
   Keep implementation conservative: main.c currently owns GDI/Window lifetime,
   so these functions only initialize/track fields in the shared context and
   do not free OS resources to avoid double-freeing. */

klo_error_t klo_graphics_init(klo_graphics_context_t *gfx) {
    KLO_CHECK_PARAM(gfx);
    memset(gfx, 0, sizeof(klo_graphics_context_t));
    return KLO_OK;
}

void klo_graphics_cleanup(klo_graphics_context_t *gfx) {
    if (!gfx) return;
    /* Do not DeleteObject() here; windows/main.c manages the bitmap lifetime.
       Zero the struct to leave a consistent state. */
    memset(gfx, 0, sizeof(klo_graphics_context_t));
}

klo_error_t klo_graphics_update_bitmap(klo_graphics_context_t *gfx, const Overlay *overlay) {
    KLO_CHECK_PARAM(gfx);
    KLO_CHECK_PARAM(overlay);
    gfx->bitmap_width = overlay->width;
    gfx->bitmap_height = overlay->height;
    return KLO_OK;
}

klo_error_t klo_ui_init(klo_ui_context_t *ui) {
    KLO_CHECK_PARAM(ui);
    memset(ui, 0, sizeof(klo_ui_context_t));
    return KLO_OK;
}

void klo_ui_cleanup(klo_ui_context_t *ui) {
    if (!ui) return;
    /* Do not DestroyWindow() here; windows/main.c manages the window lifetime.
       Zero the struct to leave a consistent state. */
    memset(ui, 0, sizeof(klo_ui_context_t));
}

klo_error_t klo_ui_create_window(klo_ui_context_t *ui, int width, int height) {
    KLO_CHECK_PARAM(ui);
    ui->width = width;
    ui->height = height;
    ui->is_visible = 0;
    ui->window = (klo_window_handle_t)NULL;
    return KLO_OK;
}

klo_error_t klo_ui_show_window(klo_ui_context_t *ui, int show) {
    KLO_CHECK_PARAM(ui);
    ui->is_visible = show;
    return KLO_OK;
}

klo_error_t klo_ui_update_position(klo_ui_context_t *ui, int x, int y) {
    KLO_CHECK_PARAM(ui);
    ui->x = x;
    ui->y = y;
    return KLO_OK;
}
