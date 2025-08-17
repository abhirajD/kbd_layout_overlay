#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

int load_config(const char *path, Config *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    cfg->overlay_path[0] = '\0';
    cfg->opacity = 1.0f;
    cfg->invert = 0;
    cfg->autostart = 0;
    cfg->hotkey[0] = '\0';
    cfg->persistent = 0;
    cfg->monitor = 0; /* auto by default */
    char line[512];
    while (fgets(line, sizeof(line), f)) {
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
            strncpy(cfg->overlay_path, val, sizeof(cfg->overlay_path) - 1);
            cfg->overlay_path[sizeof(cfg->overlay_path) - 1] = '\0';
        } else if (strcmp(key, "opacity") == 0) {
            cfg->opacity = strtof(val, NULL);
        } else if (strcmp(key, "invert") == 0) {
            cfg->invert = atoi(val);
        } else if (strcmp(key, "autostart") == 0) {
            cfg->autostart = atoi(val);
        } else if (strcmp(key, "hotkey") == 0) {
            strncpy(cfg->hotkey, val, sizeof(cfg->hotkey) - 1);
            cfg->hotkey[sizeof(cfg->hotkey) - 1] = '\0';
        } else if (strcmp(key, "persistent") == 0) {
            cfg->persistent = atoi(val);
        } else if (strcmp(key, "monitor") == 0) {
            cfg->monitor = atoi(val);
        }
    }
    fclose(f);
    return 0;
}

int save_config(const char *path, const Config *cfg) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "overlay_path=%s\n", cfg->overlay_path);
    fprintf(f, "opacity=%f\n", cfg->opacity);
    fprintf(f, "invert=%d\n", cfg->invert);
    fprintf(f, "autostart=%d\n", cfg->autostart);
    fprintf(f, "hotkey=%s\n", cfg->hotkey);
    fprintf(f, "persistent=%d\n", cfg->persistent);
    fprintf(f, "monitor=%d\n", cfg->monitor);
    fclose(f);
    return 0;
}
