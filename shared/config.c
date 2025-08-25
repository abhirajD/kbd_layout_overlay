#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _MSC_VER
#include <direct.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int write_dir_if_needed(const char *path) {
    /* Create parent dir if needed. Simple implementation: find last '/' or '\\'
       and mkdir it. Use platform functions via system call to keep code short. */
#ifdef _WIN32
    /* On Windows, use _mkdir from direct call to system if needed (keep minimal). */
    char buf[PATH_MAX];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *p = strrchr(buf, '\\');
    if (!p) p = strrchr(buf, '/');
    if (!p) return 1;
    *p = '\0';
    /* Try to create using _mkdir if available */
    #ifdef _MSC_VER
        int res = _mkdir(buf);
        (void)res;
        return 1;
    #else
        /* fallback */
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", buf);
        system(cmd);
        return 1;
    #endif
#else
    char buf[PATH_MAX];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *p = strrchr(buf, '/');
    if (!p) return 1;
    *p = '\0';
    char cmd[PATH_MAX + 32];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", buf);
    system(cmd);
    return 1;
#endif
}

const char *get_default_config_path(void) {
    static char path[PATH_MAX];
    const char *env = NULL;
#ifdef _WIN32
    env = getenv("APPDATA");
    if (!env) env = "C:\\";
    snprintf(path, sizeof(path), "%s\\kbd_layout_overlay\\config.json", env);
#else
    env = getenv("HOME");
    if (!env) env = ".";
#if defined(__APPLE__)
    snprintf(path, sizeof(path), "%s/Library/Application Support/kbd_layout_overlay/config.json", env);
#else
    snprintf(path, sizeof(path), "%s/.config/kbd_layout_overlay/config.json", env);
#endif
#endif
    path[sizeof(path)-1] = '\0';
    return path;
}

/* Internal helpers to parse a simple JSON file (non-robust but sufficient for small config) */

static const char *find_colon_after_key(const char *buf, const char *key) {
    const char *p = strstr(buf, key);
    if (!p) return NULL;
    p = strchr(p, ':');
    return p ? p + 1 : NULL;
}

static int parse_float_field(const char *buf, const char *key, float *out) {
    const char *p = find_colon_after_key(buf, key);
    if (!p) return 0;
    char *end;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    double v = strtod(p, &end);
    if (end == p) return 0;
    *out = (float)v;
    return 1;
}

static int parse_int_field(const char *buf, const char *key, int *out) {
    const char *p = find_colon_after_key(buf, key);
    if (!p) return 0;
    char *end;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    long v = strtol(p, &end, 10);
    if (end == p) return 0;
    *out = (int)v;
    return 1;
}

static int parse_string_field(const char *buf, const char *key, char *out, size_t out_sz) {
    const char *p = strstr(buf, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p != '\"') return 0;
    p++;
    const char *q = p;
    while (*q && *q != '\"') {
        if ((size_t)(q - p) + 1 >= out_sz) break;
        q++;
    }
    size_t len = (size_t)(q - p);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

int load_config(Config *out, const char *path) {
    if (!out) return -1;
    const char *cfgpath = path ? path : get_default_config_path();
    FILE *f = fopen(cfgpath, "rb");
    if (!f) {
        return 0; /* not found */
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';

    /* Start with defaults, then override if present */
    Config defaults = get_default_config();
    *out = defaults;

    int any = 0;
    if (parse_float_field(buf, "\"opacity\"", &out->opacity)) any = 1;
    if (parse_int_field(buf, "\"invert\"", &out->invert)) any = 1;
    if (parse_int_field(buf, "\"persistent\"", &out->persistent)) any = 1;
    float scale_tmp;
    if (parse_float_field(buf, "\"scale\"", &scale_tmp)) { out->scale = scale_tmp; any = 1; }

    /* Pixel-precise sizing fields */
    if (parse_int_field(buf, "\"use_custom_size\"", &out->use_custom_size)) any = 1;
    if (parse_int_field(buf, "\"custom_width_px\"", &out->custom_width_px)) any = 1;
    if (parse_int_field(buf, "\"custom_height_px\"", &out->custom_height_px)) any = 1;

    if (parse_int_field(buf, "\"position_x\"", &out->position_x)) any = 1;
    if (parse_int_field(buf, "\"position_y\"", &out->position_y)) any = 1;
    if (parse_string_field(buf, "\"hotkey\"", out->hotkey, sizeof(out->hotkey))) any = 1;

    /* New fields: auto_hide and positioning / general flags */
    if (parse_float_field(buf, "\"auto_hide\"", &out->auto_hide)) any = 1;
    if (parse_int_field(buf, "\"position_mode\"", &out->position_mode)) any = 1;
    if (parse_int_field(buf, "\"start_at_login\"", &out->start_at_login)) any = 1;
    if (parse_int_field(buf, "\"click_through\"", &out->click_through)) any = 1;
    if (parse_int_field(buf, "\"always_on_top\"", &out->always_on_top)) any = 1;
    if (parse_int_field(buf, "\"monitor_index\"", &out->monitor_index)) any = 1;

    /* Migration: legacy persistent flag maps to auto_hide == 0.0 (persistent) */
    if (out->persistent == 1) {
        out->auto_hide = 0.0f;
    }

    /* Clamp sensible ranges */
    if (out->auto_hide < 0.0f) out->auto_hide = 0.0f;
    if (out->auto_hide > 3.0f) out->auto_hide = 3.0f;
    if (out->scale < 0.5f) out->scale = 0.5f;
    if (out->scale > 2.0f) out->scale = 2.0f;
    if (out->monitor_index < 0) out->monitor_index = 0;

    free(buf);
    return any ? 1 : 0;
}

int save_config(const Config *cfg, const char *path) {
    if (!cfg) return 0;
    const char *cfgpath = path ? path : get_default_config_path();
    /* Ensure parent dir exists (best-effort) */
    write_dir_if_needed(cfgpath);

    FILE *f = fopen(cfgpath, "wb");
    if (!f) return 0;
    /* Write minimal pretty JSON including new fields */
    int res = fprintf(f,
        "{\n"
        "  \"opacity\": %.3f,\n"
        "  \"invert\": %d,\n"
        "  \"persistent\": %d,\n"
        "  \"hotkey\": \"%s\",\n"
        "  \"scale\": %.3f,\n"
        "  \"use_custom_size\": %d,\n"
        "  \"custom_width_px\": %d,\n"
        "  \"custom_height_px\": %d,\n"
        "  \"position_x\": %d,\n"
        "  \"position_y\": %d,\n"
        "  \"auto_hide\": %.3f,\n"
        "  \"position_mode\": %d,\n"
        "  \"start_at_login\": %d,\n"
        "  \"click_through\": %d,\n"
        "  \"always_on_top\": %d,\n"
        "  \"monitor_index\": %d\n"
        "}\n",
        cfg->opacity,
        cfg->invert ? 1 : 0,
        cfg->persistent ? 1 : 0,
        cfg->hotkey,
        cfg->scale,
        cfg->use_custom_size ? 1 : 0,
        cfg->custom_width_px,
        cfg->custom_height_px,
        cfg->position_x,
        cfg->position_y,
        cfg->auto_hide,
        cfg->position_mode,
        cfg->start_at_login ? 1 : 0,
        cfg->click_through ? 1 : 0,
        cfg->always_on_top ? 1 : 0,
        cfg->monitor_index
    );
    fflush(f);
    fclose(f);
    return res > 0 ? 1 : 0;
}
