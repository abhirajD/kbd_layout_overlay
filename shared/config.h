#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* Configuration */
typedef struct {
    float opacity;
    int invert;
    int persistent;         /* DEPRECATED: preserved for migration; persistent behavior == auto_hide == 0.0 */
    char hotkey[64];       /* Mutable hotkey string (UTF-8, e.g. "Ctrl+Alt+Shift+Slash") */
    float scale;           /* Image scale factor (0.5 = 50%, 1.0 = 100%, 2.0 = 200%) */
    int position_x;        /* X offset from center (-100 = 100px left, 100 = 100px right) */
    int position_y;        /* Y offset from bottom (0 = bottom, 100 = 100px from bottom) */

    /* Pixel-precise sizing */
    int use_custom_size;   /* 0 = use scale, 1 = use custom_width_px/custom_height_px */
    int custom_width_px;   /* Desired image width in pixels when use_custom_size is set */
    int custom_height_px;  /* Desired image height in pixels when use_custom_size is set */

    /* New fields for Auto-hide and positioning */
    float auto_hide;       /* seconds; 0.0 == Off / persistent. UI exposes Off / 0.8 / 2.0 */
    int position_mode;     /* 0 = Center, 1 = Top-Center, 2 = Bottom-Center, 3 = Custom */
    int start_at_login;    /* 0 = disabled, 1 = enabled (recorded only) */
    int click_through;     /* 0 = false, 1 = true (maps to setIgnoresMouseEvents:) */
    int always_on_top;     /* 0 = false, 1 = true (may map to window level) */
} Config;

/* Get default configuration */
static inline Config get_default_config(void) {
    Config config;
    config.opacity = 0.8f;
    config.invert = 0;
    config.persistent = 0;    /* kept for migration compatibility */
    config.scale = 1.0f;
    config.position_x = 0;    /* Centered */
    config.position_y = 100;  /* 100px from bottom */

    /* Pixel-precise sizing defaults: disabled by default, reasonable fallback size */
    config.use_custom_size = 0;
    config.custom_width_px = 800;
    config.custom_height_px = 600;

    /* New defaults */
    config.auto_hide = 0.8f;      /* default to 0.8s auto-hide */
    config.position_mode = 2;     /* Bottom-Center by default (matches previous behavior) */
    config.start_at_login = 0;
    config.click_through = 0;
    config.always_on_top = 0;

#ifdef _WIN32
    /* default hotkey */
    const char *hk = "Ctrl+Alt+Shift+Slash";
#else
    const char *hk = "Command+Option+Shift+Slash";
#endif
    /* copy into mutable buffer */
    size_t len = 0;
    while (hk[len] != '\0' && len + 1 < sizeof(config.hotkey)) len++;
    for (size_t i = 0; i < len; i++) config.hotkey[i] = hk[i];
    config.hotkey[len] = '\0';
    return config;
}

/* Config persistence: load and save JSON config.
   - load_config returns 1 on success, 0 if no config found (caller may use defaults), -1 on parse/error.
   - save_config returns 1 on success, 0 on failure.
*/
int load_config(Config *out, const char *path);
int save_config(const Config *cfg, const char *path);

/* Helper to get platform default config path (returns static string) */
const char *get_default_config_path(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
