#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration */
typedef struct {
    float opacity;
    int invert;
    int persistent;
    const char *hotkey;
    float scale;           /* Image scale factor (0.5 = 50%, 1.0 = 100%, 2.0 = 200%) */
    int position_x;        /* X offset from center (-100 = 100px left, 100 = 100px right) */
    int position_y;        /* Y offset from bottom (0 = bottom, 100 = 100px from bottom) */
} Config;

/* Get default configuration */
static inline Config get_default_config(void) {
    Config config;
    config.opacity = 0.8f;
    config.invert = 0;
    config.persistent = 0;
    config.scale = 1.0f;
    config.position_x = 0;    /* Centered */
    config.position_y = 100;  /* 100px from bottom */
#ifdef _WIN32
    config.hotkey = "Ctrl+Alt+Shift+Slash";
#else
    config.hotkey = "Command+Option+Shift+Slash";
#endif
    return config;
}

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */