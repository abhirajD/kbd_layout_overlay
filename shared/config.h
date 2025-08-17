#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char overlay_path[256];
    float opacity; /* 0.0 - 1.0 */
    int invert; /* 0 or 1 */
    int autostart; /* 0 or 1 */
    char hotkey[256];
    int persistent; /* 0 or 1 */
    int monitor; /* 0=auto, 1=primary, 2=secondary, etc. */
} Config;

/* Load key=value config file */
int load_config(const char *path, Config *cfg);
/* Save config back to file */
int save_config(const char *path, const Config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
