#include "hotkey_parse.h"
#include <string.h>
#include <strings.h> /* for strcasestr */

/* Simple, conservative parser that matches the token names used by the UI.
   It is intentionally permissive on casing and common aliases (Cmd/Ctrl/Alt). */

int parse_hotkey_string(const char *hk, UInt32 *out_modifiers, UInt32 *out_vk) {
    if (!hk || !out_modifiers || !out_vk) return 0;

    const char *s = hk;
    int hasModifier = 0;
    UInt32 modifiers = 0;
    UInt32 vk = 0;

    if (strcasestr(s, "command") || strcasestr(s, "cmd")) { modifiers |= cmdKey; hasModifier = 1; }
    if (strcasestr(s, "option") || strcasestr(s, "alt"))   { modifiers |= optionKey; hasModifier = 1; }
    if (strcasestr(s, "shift"))                           { modifiers |= shiftKey; hasModifier = 1; }
    if (strcasestr(s, "control") || strcasestr(s, "ctrl")){ modifiers |= controlKey; hasModifier = 1; }

    /* Map common non-modifier tokens to virtual key codes */
    if (strcasestr(s, "slash")) { vk = kVK_ANSI_Slash; }
    else if (strcasestr(s, "space")) { vk = kVK_Space; }
    else if (strcasestr(s, "return") || strcasestr(s, "enter")) { vk = kVK_Return; }
    else if (strcasestr(s, "escape") || strcasestr(s, "esc")) { vk = kVK_Escape; }
    else if (strcasestr(s, "f1")) { vk = kVK_F1; }
    else if (strcasestr(s, "f2")) { vk = kVK_F2; }
    else if (strcasestr(s, "f3")) { vk = kVK_F3; }
    else if (strcasestr(s, "f4")) { vk = kVK_F4; }
    else if (strcasestr(s, "f5")) { vk = kVK_F5; }
    else if (strcasestr(s, "f6")) { vk = kVK_F6; }
    else if (strcasestr(s, "f7")) { vk = kVK_F7; }
    else if (strcasestr(s, "f8")) { vk = kVK_F8; }
    else if (strcasestr(s, "f9")) { vk = kVK_F9; }
    else if (strcasestr(s, "f10")) { vk = kVK_F10; }
    else if (strcasestr(s, "f11")) { vk = kVK_F11; }
    else if (strcasestr(s, "f12")) { vk = kVK_F12; }
    else if (strcasestr(s, "slash")) { vk = kVK_ANSI_Slash; }
    else {
        /* Try single printable characters A-Z or punctuation by scanning for last '+' token */
        const char *lastPlus = strrchr(s, '+');
        const char *token = lastPlus ? lastPlus + 1 : s;
        /* Trim whitespace */
        while (*token == ' ' || *token == '\t') token++;
        if (token && *token) {
            char key = token[0];
            /* Map uppercase letters to ANSI keycodes if needed - fallback: do not attempt mapping */
            /* Keeping conservative: do not map arbitrary printable keys here. */
            vk = 0;
        }
    }

    if (!hasModifier || vk == 0) return 0;

    *out_modifiers = modifiers;
    *out_vk = vk;
    return 1;
}
