#include <windows.h>
#include <shellapi.h>
#include "MenuController.h"
#include "../shared/config.h"
#include "../shared/log.h"

#define WM_TRAY (WM_APP + 1)

static Config *g_config = NULL;
static NOTIFYICONDATA g_nid = {0};
static HMENU g_menu = NULL;
static void (*g_show_callback)(void) = NULL;
static void (*g_hide_callback)(void) = NULL;
static void (*g_toggle_callback)(void) = NULL;
static void (*g_config_changed_callback)(void) = NULL;

static void show_tray_menu(void) {
    if (!g_menu) return;

    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(g_nid.hWnd);
    TrackPopupMenu(g_menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN, p.x, p.y, 0, g_nid.hWnd, NULL);
}

static void rebuild_menu(void) {
    if (g_menu) {
        DestroyMenu(g_menu);
    }

    g_menu = CreatePopupMenu();

    /* Show Keymap toggle with state indicator */
    UINT showKeymapFlags = MF_STRING;
    if (g_toggle_callback) {
        // We don't have visibility state here, so just show the option
        showKeymapFlags |= MF_STRING;
    }
    AppendMenuA(g_menu, showKeymapFlags, 101, "Show Keymap");

    AppendMenuA(g_menu, MF_SEPARATOR, 0, NULL);

    /* Scale submenu */
    HMENU scaleMenu = CreatePopupMenu();
    AppendMenuA(scaleMenu, MF_STRING | (fabsf(g_config->scale - 0.75f) < 0.001f ? MF_CHECKED : 0), 201, "75%");
    AppendMenuA(scaleMenu, MF_STRING | (fabsf(g_config->scale - 1.0f) < 0.001f ? MF_CHECKED : 0), 202, "100%");
    AppendMenuA(scaleMenu, MF_STRING | (fabsf(g_config->scale - 1.25f) < 0.001f ? MF_CHECKED : 0), 203, "125%");
    AppendMenuA(scaleMenu, MF_STRING | (fabsf(g_config->scale - 1.5f) < 0.001f ? MF_CHECKED : 0), 204, "150%");
    AppendMenuA(scaleMenu, MF_STRING, 205, "Fit Screen");
    AppendMenuA(g_menu, MF_STRING | MF_POPUP, (UINT_PTR)scaleMenu, "Scale");

    /* Opacity submenu */
    HMENU opacityMenu = CreatePopupMenu();
    AppendMenuA(opacityMenu, MF_STRING | (fabsf(g_config->opacity - 0.5f) < 0.001f ? MF_CHECKED : 0), 301, "50%");
    AppendMenuA(opacityMenu, MF_STRING | (fabsf(g_config->opacity - 0.7f) < 0.001f ? MF_CHECKED : 0), 302, "70%");
    AppendMenuA(opacityMenu, MF_STRING | (fabsf(g_config->opacity - 0.85f) < 0.001f ? MF_CHECKED : 0), 303, "85%");
    AppendMenuA(opacityMenu, MF_STRING | (fabsf(g_config->opacity - 1.0f) < 0.001f ? MF_CHECKED : 0), 304, "100%");
    AppendMenuA(g_menu, MF_STRING | MF_POPUP, (UINT_PTR)opacityMenu, "Opacity");

    /* Auto-hide submenu */
    HMENU autoHideMenu = CreatePopupMenu();
    AppendMenuA(autoHideMenu, MF_STRING | (g_config->auto_hide == 0.0f ? MF_CHECKED : 0), 401, "Off");
    AppendMenuA(autoHideMenu, MF_STRING | (fabsf(g_config->auto_hide - 0.8f) < 0.001f ? MF_CHECKED : 0), 402, "0.8s");
    AppendMenuA(autoHideMenu, MF_STRING | (fabsf(g_config->auto_hide - 2.0f) < 0.001f ? MF_CHECKED : 0), 403, "2.0s");
    AppendMenuA(autoHideMenu, MF_STRING, 404, "Custom...");
    AppendMenuA(g_menu, MF_STRING | MF_POPUP, (UINT_PTR)autoHideMenu, "Auto-hide");

    /* Monitor submenu */
    HMENU monitorMenu = CreatePopupMenu();
    int mcount = 1; // Simplified for now
    for (int i = 0; i < mcount; i++) {
        char label[32];
        snprintf(label, sizeof(label), "Monitor %d", i + 1);
        UINT flags = MF_STRING | (i == g_config->monitor_index ? MF_CHECKED : 0);
        AppendMenuA(monitorMenu, flags, 500 + i, label);
    }
    AppendMenuA(g_menu, MF_STRING | MF_POPUP, (UINT_PTR)monitorMenu, "Monitor");

    AppendMenuA(g_menu, MF_SEPARATOR, 0, NULL);

    /* Preview Keymap */
    AppendMenuA(g_menu, MF_STRING, 102, "Preview Keymap");

    AppendMenuA(g_menu, MF_SEPARATOR, 0, NULL);

    /* Preferences */
    AppendMenuA(g_menu, MF_STRING, 8, "Preferences...");
    AppendMenuA(g_menu, MF_STRING, 3, "Quit");
}

int menu_controller_init(Config *config, HINSTANCE hInstance, HWND hwnd) {
    g_config = config;

    /* Setup tray icon */
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(g_nid.szTip, "KbdLayoutOverlay");

    if (!Shell_NotifyIcon(NIM_ADD, &g_nid)) {
        logger_log("Failed to create tray icon");
        return 0;
    }

    rebuild_menu();
    logger_log("Menu controller initialized");
    return 1;
}

void menu_controller_cleanup(void) {
    if (g_menu) {
        DestroyMenu(g_menu);
        g_menu = NULL;
    }
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
    logger_log("Menu controller cleaned up");
}

void menu_controller_set_callbacks(
    void (*show_callback)(void),
    void (*hide_callback)(void),
    void (*toggle_callback)(void),
    void (*config_changed_callback)(void)
) {
    g_show_callback = show_callback;
    g_hide_callback = hide_callback;
    g_toggle_callback = toggle_callback;
    g_config_changed_callback = config_changed_callback;
}

void menu_controller_handle_command(int command) {
    switch (command) {
        /* Show Keymap toggle */
        case 101:
            if (g_toggle_callback) g_toggle_callback();
            break;
        /* Preview Keymap */
        case 102:
            if (g_show_callback) g_show_callback();
            // Auto-hide after 3 seconds would be handled by caller
            break;
        /* Scale options */
        case 201: /* 75% */
            g_config->scale = 0.75f;
            g_config->use_custom_size = 0;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        case 202: /* 100% */
            g_config->scale = 1.0f;
            g_config->use_custom_size = 0;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        case 203: /* 125% */
            g_config->scale = 1.25f;
            g_config->use_custom_size = 0;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        case 204: /* 150% */
            g_config->scale = 1.5f;
            g_config->use_custom_size = 0;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        case 205: /* Fit Screen */ {
            /* Calculate scale to fit 80% of primary monitor width */
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            float targetWidth = screenWidth * 0.8f;
            // This would need overlay dimensions from ImageManager
            break;
        }
        /* Opacity options */
        case 301: /* 50% */
            g_config->opacity = 0.5f;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        case 302: /* 70% */
            g_config->opacity = 0.7f;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        case 303: /* 85% */
            g_config->opacity = 0.85f;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        case 304: /* 100% */
            g_config->opacity = 1.0f;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        /* Auto-hide options */
        case 401: /* Off */
            g_config->auto_hide = 0.0f;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        case 402: /* 0.8s */
            g_config->auto_hide = 0.8f;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        case 403: /* 2.0s */
            g_config->auto_hide = 2.0f;
            if (g_config_changed_callback) g_config_changed_callback();
            break;
        case 404: /* Custom... */
            // Open preferences would be handled by caller
            break;
        case 8: /* Preferences */
            // Open preferences would be handled by caller
            break;
        case 3: /* Quit */
            PostQuitMessage(0);
            break;
        default:
            if (command >= 500 && command < 510) {
                g_config->monitor_index = command - 500;
                if (g_config_changed_callback) g_config_changed_callback();
            }
            break;
    }
}

void menu_controller_show_menu(void) {
    show_tray_menu();
}

void menu_controller_update_menu(void) {
    rebuild_menu();
}
