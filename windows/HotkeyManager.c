#include <windows.h>
#include <ctype.h>
#include <string.h>
#include "HotkeyManager.h"
#include "../shared/config.h"
#include "../shared/log.h"

static Config *g_config = NULL;
static HHOOK g_kb_hook = NULL;
static UINT g_hook_modifiers = 0;
static UINT g_hook_vk = 0;
static void (*g_toggle_callback)(void) = NULL;

typedef struct {
    const char *name;
    UINT vk;
} KeyMapEntry;

static const KeyMapEntry g_key_map[] = {
    {"Slash",    VK_OEM_2},
    {"Space",    VK_SPACE},
    {"Enter",    VK_RETURN},
    {"Escape",   VK_ESCAPE},
    {"Tab",      VK_TAB},
    {"Backspace",VK_BACK},
    {"Delete",   VK_DELETE},
    {"Insert",   VK_INSERT},
    {"Home",     VK_HOME},
    {"End",      VK_END},
    {"PgUp",     VK_PRIOR},
    {"PageUp",   VK_PRIOR},
    {"PgDown",   VK_NEXT},
    {"PageDown", VK_NEXT},
    {"Up",       VK_UP},
    {"Down",     VK_DOWN},
    {"Left",     VK_LEFT},
    {"Right",    VK_RIGHT},
    {"F1",       VK_F1},
    {"F2",       VK_F2},
    {"F3",       VK_F3},
    {"F4",       VK_F4},
    {"F5",       VK_F5},
    {"F6",       VK_F6},
    {"F7",       VK_F7},
    {"F8",       VK_F8},
    {"F9",       VK_F9},
    {"F10",      VK_F10},
    {"F11",      VK_F11},
    {"F12",      VK_F12},
    {NULL, 0}
};

static int strieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *pk = (KBDLLHOOKSTRUCT *)lParam;
        int is_key_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

        /* Skip key repeat events */
        static DWORD last_time = 0;
        static UINT last_vk = 0;
        DWORD current_time = GetTickCount();

        if (pk && is_key_down && pk->vkCode == last_vk && (current_time - last_time) < 50) {
            return CallNextHookEx(g_kb_hook, nCode, wParam, lParam);
        }

        if (pk) {
            /* Match configured key */
            if ((UINT)pk->vkCode == g_hook_vk) {
                /* Update repeat detection */
                last_time = current_time;
                last_vk = pk->vkCode;

                /* Check modifier state */
                int ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                int alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                int shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                int win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
                UINT mods = 0;
                if (ctrl) mods |= MOD_CONTROL;
                if (alt) mods |= MOD_ALT;
                if (shift) mods |= MOD_SHIFT;
                if (win) mods |= MOD_WIN;

                /* Use subset match so extra modifiers don't break detection */
                if ((mods & g_hook_modifiers) == g_hook_modifiers) {
                    if (is_key_down) {
                        if (g_toggle_callback) {
                            g_toggle_callback();
                        }
                    }
                }
            }
        }
    }
    return CallNextHookEx(g_kb_hook, nCode, wParam, lParam);
}

int hotkey_manager_init(Config *config) {
    g_config = config;
    return 1;
}

void hotkey_manager_cleanup(void) {
    if (g_kb_hook) {
        UnhookWindowsHookEx(g_kb_hook);
        g_kb_hook = NULL;
    }
}

int hotkey_manager_register_hotkey(void) {
    /* Parse hotkey string */
    UINT modifiers = 0, vk = 0;
    if (!hotkey_manager_parse_hotkey(g_config->hotkey, &modifiers, &vk)) {
        logger_log("Invalid hotkey: %s", g_config->hotkey);
        return 0;
    }

    /* Update hook mapping */
    g_hook_modifiers = modifiers;
    g_hook_vk = vk;

    /* Install keyboard hook */
    if (!g_kb_hook) {
        g_kb_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                    GetModuleHandle(NULL), 0);
        if (!g_kb_hook) {
            logger_log("Failed to install keyboard hook");
            return 0;
        }
    }

    logger_log("Hotkey registered: %s (vk=%u mods=0x%04x)", g_config->hotkey, vk, modifiers);
    return 1;
}

void hotkey_manager_unregister_hotkey(void) {
    if (g_kb_hook) {
        UnhookWindowsHookEx(g_kb_hook);
        g_kb_hook = NULL;
        logger_log("Hotkey unregistered");
    }
}

int hotkey_manager_parse_hotkey(const char *hotkey_str, UINT *modifiers, UINT *vk) {
    if (!hotkey_str || !modifiers || !vk) return 0;
    *modifiers = 0;
    *vk = 0;

    char buf[128];
    strncpy(buf, hotkey_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (char *token = strtok(buf, "+"); token; token = strtok(NULL, "+")) {
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token);
        while (end > token && (end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }

        if (strieq(token, "Ctrl") || strieq(token, "Control")) {
            *modifiers |= MOD_CONTROL;
        } else if (strieq(token, "Alt")) {
            *modifiers |= MOD_ALT;
        } else if (strieq(token, "Shift")) {
            *modifiers |= MOD_SHIFT;
        } else if (strieq(token, "Win") || strieq(token, "Windows")) {
            *modifiers |= MOD_WIN;
        } else {
            for (const KeyMapEntry *km = g_key_map; km->name; ++km) {
                if (strieq(token, km->name)) {
                    *vk = km->vk;
                    break;
                }
            }
            if (*vk == 0 && strlen(token) == 1) {
                unsigned char c = (unsigned char)token[0];
                if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
                if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                    *vk = (UINT)c;
                }
            }
        }
    }

    /* Require at least one modifier and a recognized non-modifier key */
    if (*modifiers == 0 || *vk == 0) {
        return 0;
    }
    return 1;
}

void hotkey_manager_set_toggle_callback(void (*callback)(void)) {
    g_toggle_callback = callback;
}

int hotkey_manager_is_valid_hotkey(const char *hotkey_str) {
    UINT modifiers = 0, vk = 0;
    return hotkey_manager_parse_hotkey(hotkey_str, &modifiers, &vk);
}
