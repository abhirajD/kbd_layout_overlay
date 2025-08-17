#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_LINE_LENGTH 1024
#define MAX_CONFIG_VALUE_LENGTH 512

static void trim(char *s) {
    char *p = s;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) {
        s[len-1] = '\0';
        len--;
    }
}

klo_error_t init_config(Config *cfg) {
    KLO_CHECK_PARAM(cfg);
    
    cfg->overlay_path = NULL;
    cfg->hotkey = NULL;
    cfg->opacity = 1.0f;
    cfg->invert = 0;
    cfg->autostart = 0;
    cfg->persistent = 0;
    cfg->monitor = 0; /* auto by default */
    
    /* Set default values */
    klo_error_t err = set_config_string(&cfg->overlay_path, "keymap.png");
    if (err != KLO_OK) return err;
    
    err = set_config_string(&cfg->hotkey, "Ctrl+Alt+Shift+Slash");
    if (err != KLO_OK) return err;
    
    return KLO_OK;
}

void free_config(Config *cfg) {
    if (!cfg) return;
    free(cfg->overlay_path);
    free(cfg->hotkey);
    cfg->overlay_path = NULL;
    cfg->hotkey = NULL;
}

klo_error_t set_config_string(char **field, const char *value) {
    KLO_CHECK_PARAM(field);
    KLO_CHECK_PARAM(value);
    
    size_t len = strlen(value);
    if (len > MAX_CONFIG_VALUE_LENGTH) {
        KLO_ERROR_RETURN(KLO_ERR_CONFIG_INVALID, "Configuration value too long");
    }
    
    char *new_str = malloc(len + 1);
    KLO_CHECK_MEMORY(new_str);
    
    strcpy(new_str, value);
    free(*field);
    *field = new_str;
    return KLO_OK;
}

klo_error_t load_config(const char *path, Config *cfg) {
    KLO_CHECK_PARAM(path);
    KLO_CHECK_PARAM(cfg);
    
    FILE *f = fopen(path, "r");
    if (!f) {
        KLO_ERROR_RETURN(KLO_ERR_CONFIG_IO, "Failed to open configuration file");
    }
    
    char line[MAX_LINE_LENGTH];
    while (fgets(line, MAX_LINE_LENGTH, f)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);
        if (strcmp(key, "overlay_path") == 0) {
            klo_error_t err = set_config_string(&cfg->overlay_path, val);
            if (err != KLO_OK) {
                fclose(f);
                return err;
            }
        } else if (strcmp(key, "opacity") == 0) {
            cfg->opacity = strtof(val, NULL);
        } else if (strcmp(key, "invert") == 0) {
            cfg->invert = atoi(val);
        } else if (strcmp(key, "autostart") == 0) {
            cfg->autostart = atoi(val);
        } else if (strcmp(key, "hotkey") == 0) {
            klo_error_t err = set_config_string(&cfg->hotkey, val);
            if (err != KLO_OK) {
                fclose(f);
                return err;
            }
        } else if (strcmp(key, "persistent") == 0) {
            cfg->persistent = atoi(val);
        } else if (strcmp(key, "monitor") == 0) {
            cfg->monitor = atoi(val);
        }
    }
    fclose(f);
    return KLO_OK;
}

klo_error_t save_config(const char *path, const Config *cfg) {
    KLO_CHECK_PARAM(path);
    KLO_CHECK_PARAM(cfg);
    
    FILE *f = fopen(path, "w");
    if (!f) {
        KLO_ERROR_RETURN(KLO_ERR_CONFIG_IO, "Failed to create configuration file");
    }
    fprintf(f, "overlay_path=%s\n", cfg->overlay_path);
    fprintf(f, "opacity=%f\n", cfg->opacity);
    fprintf(f, "invert=%d\n", cfg->invert);
    fprintf(f, "autostart=%d\n", cfg->autostart);
    fprintf(f, "hotkey=%s\n", cfg->hotkey);
    fprintf(f, "persistent=%d\n", cfg->persistent);
    fprintf(f, "monitor=%d\n", cfg->monitor);
    fclose(f);
    return KLO_OK;
}
