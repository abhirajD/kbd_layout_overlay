#ifndef PLATFORM_H
#define PLATFORM_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Platform-agnostic interface for cross-platform functionality
typedef struct PlatformAPI {
    // Window management
    int (*init_window)(Config *config);
    void (*cleanup_window)(void);
    void (*show_overlay)(void);
    void (*hide_overlay)(void);
    void (*toggle_overlay)(void);
    int (*is_overlay_visible)(void);

    // Image management
    int (*init_image)(Config *config);
    void (*cleanup_image)(void);
    int (*load_overlay)(void);
    int (*reload_overlay_if_needed)(void);
    void (*apply_image_effects)(void);

    // Hotkey management
    int (*init_hotkey)(Config *config);
    void (*cleanup_hotkey)(void);
    int (*register_hotkey)(void);
    void (*unregister_hotkey)(void);
    void (*set_hotkey_callback)(void (*callback)(void));

    // Menu management
    int (*init_menu)(Config *config);
    void (*cleanup_menu)(void);
    void (*show_menu)(void);
    void (*set_menu_callbacks)(void (*show_cb)(void), void (*hide_cb)(void),
                              void (*toggle_cb)(void), void (*config_cb)(void));

    // System utilities
    void (*run_event_loop)(void);
    void (*exit_event_loop)(void);
} PlatformAPI;

// Get the platform-specific API implementation
PlatformAPI* get_platform_api(void);

// Performance optimization functions
void optimize_memory_usage(void);
void optimize_rendering_pipeline(void);
int enable_hardware_acceleration(void);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_H
