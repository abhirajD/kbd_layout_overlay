#ifndef CONFIG_H
#define CONFIG_H

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *overlay_path;
    float opacity; /* 0.0 - 1.0 */
    int invert; /* 0 or 1 */
    int autostart; /* 0 or 1 */
    char *hotkey;
    int persistent; /* 0 or 1 */
    int monitor; /* 0=auto, 1=primary, 2=secondary, etc. */
} Config;

/* Initialize config with defaults */
klo_error_t init_config(Config *cfg);
/* Free config memory */
void free_config(Config *cfg);
/* Load key=value config file */
klo_error_t load_config(const char *path, Config *cfg);
/* Save config back to file */
klo_error_t save_config(const char *path, const Config *cfg);
/* Set config string field safely */
klo_error_t set_config_string(char **field, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
