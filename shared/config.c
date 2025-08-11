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
        }
    }
    fclose(f);
    return 0;
}
