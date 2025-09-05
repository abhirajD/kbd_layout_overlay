#include "system_win.h"
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include "config.h"
#include "log.h"

struct SystemTray {
    NOTIFYICONDATAA notifyIconData;
    HWND hiddenWindow;
    BOOL isCreated;
};

struct HotkeyHandler {
    HHOOK keyboardHook;
    UINT modifiers;
    UINT vk;
    void (*callback)(void);
};

static HotkeyHandler *g_current_hotkey_handler = NULL;

/* Case-insensitive string comparison */
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

static int parse_hotkey(const char *hotkey_str, UINT *modifiers, UINT *vk) {
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

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_current_hotkey_handler) {
        KBDLLHOOKSTRUCT *pk = (KBDLLHOOKSTRUCT *)lParam;
        int is_key_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        int is_key_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        /* Skip key repeat events */
        static DWORD last_time = 0;
        static UINT last_vk = 0;
        DWORD current_time = GetTickCount();

        if (pk && is_key_down && pk->vkCode == last_vk && (current_time - last_time) < 50) {
            return CallNextHookEx(g_current_hotkey_handler->keyboardHook, nCode, wParam, lParam);
        }

        if (pk && (UINT)pk->vkCode == g_current_hotkey_handler->vk) {
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
            if ((mods & g_current_hotkey_handler->modifiers) == g_current_hotkey_handler->modifiers) {
                if (is_key_down && g_current_hotkey_handler->callback) {
                    g_current_hotkey_handler->callback();
                }
            }
        }
    }
    return CallNextHookEx(g_current_hotkey_handler ? g_current_hotkey_handler->keyboardHook : NULL, nCode, wParam, lParam);
}

SystemTray* create_system_tray(void) {
    SystemTray *tray = calloc(1, sizeof(SystemTray));
    if (!tray) return NULL;

    // Create hidden window for tray messages
    tray->hiddenWindow = CreateWindowExA(0, "STATIC", "", 0, 0, 0, 0, 0,
                                        HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL);
    if (!tray->hiddenWindow) {
        free(tray);
        return NULL;
    }

    // Initialize tray icon
    tray->notifyIconData.cbSize = sizeof(NOTIFYICONDATAA);
    tray->notifyIconData.hWnd = tray->hiddenWindow;
    tray->notifyIconData.uID = 1;
    tray->notifyIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray->notifyIconData.uCallbackMessage = WM_APP + 1;
    tray->notifyIconData.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(tray->notifyIconData.szTip, "Keyboard Layout Overlay");

    if (Shell_NotifyIconA(NIM_ADD, &tray->notifyIconData)) {
        tray->isCreated = TRUE;
        return tray;
    } else {
        DestroyWindow(tray->hiddenWindow);
        free(tray);
        return NULL;
    }
}

void destroy_system_tray(SystemTray* tray) {
    if (!tray) return;

    if (tray->isCreated) {
        Shell_NotifyIconA(NIM_DELETE, &tray->notifyIconData);
    }

    if (tray->hiddenWindow) {
        DestroyWindow(tray->hiddenWindow);
    }

    free(tray);
}

void update_tray_menu(SystemTray* tray) {
    if (!tray || !tray->isCreated) return;

    // Tray menu updates would be implemented here
    // For now, the menu is static
}

HotkeyHandler* register_hotkey(const char* hotkey_str, void(*callback)(void)) {
    if (!hotkey_str || !callback) return NULL;

    HotkeyHandler *handler = calloc(1, sizeof(HotkeyHandler));
    if (!handler) return NULL;

    handler->callback = callback;

    if (!parse_hotkey(hotkey_str, &handler->modifiers, &handler->vk)) {
        free(handler);
        return NULL;
    }

    // Install keyboard hook
    handler->keyboardHook = SetWindowsHookExA(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                             GetModuleHandle(NULL), 0);
    if (!handler->keyboardHook) {
        free(handler);
        return NULL;
    }

    g_current_hotkey_handler = handler;
    logger_log("Windows hotkey registered: %s", hotkey_str);

    return handler;
}

void unregister_hotkey(HotkeyHandler* handler) {
    if (!handler) return;

    if (handler->keyboardHook) {
        UnhookWindowsHookEx(handler->keyboardHook);
        handler->keyboardHook = NULL;
    }

    if (g_current_hotkey_handler == handler) {
        g_current_hotkey_handler = NULL;
    }

    free(handler);
    logger_log("Windows hotkey unregistered");
}

void show_notification(const char* title, const char* message) {
    if (!title || !message) return;

    NOTIFYICONDATAA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    strncpy(nid.szInfoTitle, title, sizeof(nid.szInfoTitle) - 1);
    strncpy(nid.szInfo, message, sizeof(nid.szInfo) - 1);

    // We need a window handle to show the notification
    // For now, we'll skip this as it requires more complex setup
    // Shell_NotifyIconA(NIM_MODIFY, &nid);
}
