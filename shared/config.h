#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration - hardcoded defaults only */
typedef struct {
    float opacity;
    int invert;
    int persistent;
    const char *hotkey;
} Config;

/* Get default configuration */
static inline Config get_default_config(void) {
    Config config;
    config.opacity = 0.8f;
    config.invert = 0;
    config.persistent = 0;
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