#include "hotkey.h"
#include <string.h>
#include <ctype.h>

void parse_hotkey(const char *str, hotkey_t *out) {
    out->mods = 0;
    out->key = '/';
    if (!str) return;

    char buf[256];
    strncpy(buf, str, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    char *token = strtok(buf, "+");
    while (token) {
        for (char *p = token; *p; ++p) *p = (char)tolower((unsigned char)*p);

        if (!strcmp(token, "ctrl") || !strcmp(token, "control")) {
            out->mods |= HOTKEY_MOD_CTRL;
        } else if (!strcmp(token, "alt") || !strcmp(token, "option") || !strcmp(token, "opt")) {
            out->mods |= HOTKEY_MOD_ALT;
        } else if (!strcmp(token, "shift")) {
            out->mods |= HOTKEY_MOD_SHIFT;
        } else if (!strcmp(token, "win") || !strcmp(token, "windows") ||
                   !strcmp(token, "cmd") || !strcmp(token, "command") ||
                   !strncmp(token, "meta", 4)) {
            out->mods |= HOTKEY_MOD_SUPER;
        } else {
            size_t len = strlen(token);
            if (len == 1) {
                char c = token[0];
                if (c >= 'a' && c <= 'z') out->key = 'A' + (c - 'a');
                else if (c >= 'A' && c <= 'Z') out->key = c;
                else if (c >= '0' && c <= '9') out->key = c;
                else if (c == '/') out->key = '/';
            } else if (!strcmp(token, "slash")) {
                out->key = '/';
            }
        }
        token = strtok(NULL, "+");
    }
}
