#ifndef HOTKEY_H
#define HOTKEY_H

#include <stdint.h>

#define HOTKEY_MOD_CTRL  (1u << 0)
#define HOTKEY_MOD_ALT   (1u << 1)
#define HOTKEY_MOD_SHIFT (1u << 2)
#define HOTKEY_MOD_SUPER (1u << 3)

typedef struct {
    uint32_t mods;
    char key; /* ASCII representation of non-modifier key */
} hotkey_t;

void parse_hotkey(const char *str, hotkey_t *out);

#endif /* HOTKEY_H */
