#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include <stdint.h>
#include <stddef.h>

#include "config.h"
#include "overlay.h"
#include "error.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for platform-specific types */
#ifdef _WIN32
typedef HWND klo_window_handle_t;
typedef HDC klo_device_context_t;
typedef HBITMAP klo_bitmap_t;
typedef HHOOK klo_hook_t;
typedef NOTIFYICONDATAA klo_tray_icon_t;
typedef UINT klo_vkey_t;
#elif defined(__APPLE__)
typedef void* klo_window_handle_t;  /* NSPanel* */
typedef void* klo_device_context_t; /* CGContextRef */
typedef void* klo_bitmap_t;         /* CGImageRef */
typedef void* klo_hook_t;           /* EventHotKeyRef */
typedef void* klo_tray_icon_t;      /* NSStatusItem* */
typedef uint32_t klo_vkey_t;
#else
typedef void* klo_window_handle_t;
typedef void* klo_device_context_t;
typedef void* klo_bitmap_t;
typedef void* klo_hook_t;
typedef void* klo_tray_icon_t;
typedef uint32_t klo_vkey_t;
#endif

/* Graphics context for rendering operations */
typedef struct {
    klo_device_context_t screen_dc;     /* Screen device context */
    klo_device_context_t mem_dc;        /* Memory device context */
    klo_bitmap_t bitmap;                /* Current overlay bitmap */
    void *bitmap_bits;                  /* Direct access to bitmap data */
    int bitmap_width;                   /* Current bitmap dimensions */
    int bitmap_height;
} klo_graphics_context_t;

/* Hotkey management state */
typedef struct {
    klo_vkey_t virtual_key;             /* Platform-specific virtual key */
    uint32_t modifiers;                 /* Platform-specific modifier flags */
    klo_hook_t hook;                    /* Low-level keyboard hook (Windows) */
    int is_active;                      /* Whether hotkey is currently pressed */
    int is_registered;                  /* Whether hotkey is registered with system */
} klo_hotkey_context_t;

/* Window and UI state */
typedef struct {
    klo_window_handle_t window;         /* Main overlay window */
    klo_tray_icon_t tray_icon;          /* System tray icon */
    int is_visible;                     /* Current visibility state */
    int x, y;                           /* Window position */
    int width, height;                  /* Window dimensions */
} klo_ui_context_t;

/* Main application context - contains all application state */
typedef struct {
    /* Core configuration and data */
    Config config;                      /* Application configuration */
    char *config_path;                  /* Path to configuration file */
    
    /* Image and overlay management */
    Overlay overlay;                    /* Main overlay image */
    OverlayCache overlay_cache;         /* Cached overlay variations */
    
    /* Platform-specific contexts */
    klo_graphics_context_t graphics;    /* Graphics rendering state */
    klo_hotkey_context_t hotkey;        /* Hotkey management */
    klo_ui_context_t ui;                /* Window and UI state */
    
    /* Application state flags */
    int is_initialized;                 /* Whether context is fully initialized */
    int should_exit;                    /* Exit flag for main loop */
    
    /* Platform-specific extension data */
    void *platform_data;               /* Platform-specific context extension */
} klo_app_context_t;

/* Context lifecycle management */
klo_error_t klo_context_init(klo_app_context_t *ctx, const char *config_path);
void klo_context_free(klo_app_context_t *ctx);

/* Context state management */
klo_error_t klo_context_load_config(klo_app_context_t *ctx);
klo_error_t klo_context_save_config(klo_app_context_t *ctx);
klo_error_t klo_context_reload_overlay(klo_app_context_t *ctx);

/* Graphics context helpers */
klo_error_t klo_graphics_init(klo_graphics_context_t *gfx);
void klo_graphics_cleanup(klo_graphics_context_t *gfx);
klo_error_t klo_graphics_update_bitmap(klo_graphics_context_t *gfx, const Overlay *overlay);

/* Hotkey context helpers */
klo_error_t klo_hotkey_init(klo_hotkey_context_t *hk, const char *hotkey_string);
void klo_hotkey_cleanup(klo_hotkey_context_t *hk);
klo_error_t klo_hotkey_register(klo_hotkey_context_t *hk, klo_window_handle_t window);
klo_error_t klo_hotkey_unregister(klo_hotkey_context_t *hk);

/* UI context helpers */
klo_error_t klo_ui_init(klo_ui_context_t *ui);
void klo_ui_cleanup(klo_ui_context_t *ui);
klo_error_t klo_ui_create_window(klo_ui_context_t *ui, int width, int height);
klo_error_t klo_ui_show_window(klo_ui_context_t *ui, int show);
klo_error_t klo_ui_update_position(klo_ui_context_t *ui, int x, int y);

#ifdef __cplusplus
}
#endif

#endif /* APP_CONTEXT_H */
