#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "config.h"
#include "overlay.h"
#include "log.h"

static int g_key_pressed = 0;

#define WM_TRAY (WM_APP + 1)
#define HOTKEY_ID 1

static Config g_config;
static Overlay g_overlay;
static HWND g_window = NULL;
static HWND g_hidden_window = NULL;
static HBITMAP g_bitmap = NULL;
static HDC g_screen_dc = NULL;
static HDC g_mem_dc = NULL;
static void *g_bitmap_bits = NULL;
static int g_visible = 0;
static int g_hotkey_pressed = 0;
static HHOOK g_kb_hook = NULL;
static UINT g_hook_modifiers = 0;
static UINT g_hook_vk = 0;
#define MSG_SHOW_OVERLAY (WM_USER + 101)
#define MSG_HIDE_OVERLAY (WM_USER + 102)

/* Preferences window controls */
static HWND g_prefs_window = NULL;
static HWND g_prefs_hotkey_edit = NULL;
static HWND g_prefs_opacity_edit = NULL;
static HWND g_prefs_custom_size_check = NULL;
static HWND g_prefs_custom_width_edit = NULL;
static HWND g_prefs_custom_height_edit = NULL;

/* New enhanced preferences controls */
static HWND g_scale_slider = NULL;
static HWND g_scale_label = NULL;
static HWND g_opacity_slider = NULL;
static HWND g_opacity_label = NULL;
static HWND g_autohide_slider = NULL;
static HWND g_autohide_label = NULL;

/* Original overlay image (PNG data) and last-used sizing config */
static unsigned char *g_original_image = NULL;
static int g_original_image_size = 0;
static float g_last_scale = -1.0f;
static int g_last_custom_width = -1;
static int g_last_custom_height = -1;
static int g_last_use_custom = -1;

/* Control IDs */
#define ID_PREFS_OPEN 8
#define IDC_PREFS_OK 101
#define IDC_PREFS_CANCEL 102
#define IDC_PREF_HOTKEY 201
#define IDC_PREF_OPACITY 202
#define IDC_USE_CUSTOM_SIZE 203
#define IDC_CUSTOM_WIDTH 204
#define IDC_CUSTOM_HEIGHT 205

/* New enhanced preferences controls */
#define IDC_SCALE_SLIDER 206
#define IDC_SCALE_LABEL 207
#define IDC_OPACITY_SLIDER 208
#define IDC_OPACITY_LABEL 209
#define IDC_AUTOHIDE_SLIDER 210
#define IDC_AUTOHIDE_LABEL 211

/* Forward declarations for label update helpers */
static void update_scale_label(void);
static void update_opacity_label(void);
static void update_autohide_label(void);

static void cleanup_resources(void) {
    if (g_bitmap) DeleteObject(g_bitmap);
    if (g_mem_dc) DeleteDC(g_mem_dc);
    if (g_screen_dc) ReleaseDC(NULL, g_screen_dc);
    free_overlay(&g_overlay);
    if (g_original_image) {
        free(g_original_image);
        g_original_image = NULL;
        g_original_image_size = 0;
    }
}

/* Get virtual desktop bounds (multi-monitor support from working example) */
typedef struct {
    int target;
    int index;
    RECT rect;
    int found;
} MonitorEnumData;

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC hdc, LPRECT lprcMonitor, LPARAM lParam) {
    MonitorEnumData *data = (MonitorEnumData *)lParam;
    if (data->index == data->target) {
        data->rect = *lprcMonitor;
        data->found = 1;
        return FALSE;
    }
    data->index++;
    return TRUE;
}

static RECT get_monitor_rect(int monitor_index) {
    MonitorEnumData data = { monitor_index, 0, {0,0,0,0}, 0 };
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&data);
    if (!data.found) {
        data.rect.left = 0;
        data.rect.top = 0;
        data.rect.right = GetSystemMetrics(SM_CXSCREEN);
        data.rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return data.rect;
}

static BOOL CALLBACK CountMonitorsProc(HMONITOR hMon, HDC hdc, LPRECT lprc, LPARAM lParam) {
    int *count = (int *)lParam;
    (*count)++;
    return TRUE;
}

static int get_monitor_count(void) {
    int count = 0;
    EnumDisplayMonitors(NULL, NULL, CountMonitorsProc, (LPARAM)&count);
    if (count <= 0) count = 1;
    return count;
}

/* Load and cache the original PNG data for the overlay image */
static int ensure_original_image(void) {
    if (g_original_image) return 1;

    const char *search_paths[] = {
        "keymap.png",
        "assets\\keymap.png",
        "..\\assets\\keymap.png",
        NULL
    };

    for (int i = 0; search_paths[i]; i++) {
        FILE *f = fopen(search_paths[i], "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0) {
                g_original_image = (unsigned char *)malloc((size_t)sz);
                if (!g_original_image) {
                    fclose(f);
                    return 0;
                }
                if (fread(g_original_image, 1, (size_t)sz, f) == (size_t)sz) {
                    g_original_image_size = (int)sz;
                    fclose(f);
                    return 1;
                }
                free(g_original_image);
                g_original_image = NULL;
            }
            fclose(f);
        }
    }

    int size = 0;
    const unsigned char *data = get_default_keymap(&size);
    if (data && size > 0) {
        g_original_image = (unsigned char *)malloc((size_t)size);
        if (!g_original_image) return 0;
        memcpy(g_original_image, data, (size_t)size);
        g_original_image_size = size;
        return 1;
    }

    return 0;
}

static int parse_hotkey(const char *hotkey_str, UINT *modifiers, UINT *vk) {
    if (!hotkey_str || !modifiers || !vk) return 0;
    *modifiers = 0;
    *vk = 0;
    
    if (strstr(hotkey_str, "Ctrl")) *modifiers |= MOD_CONTROL;
    if (strstr(hotkey_str, "Alt")) *modifiers |= MOD_ALT;
    if (strstr(hotkey_str, "Shift")) *modifiers |= MOD_SHIFT;
    if (strstr(hotkey_str, "Win")) *modifiers |= MOD_WIN;
    
    /* Support common keys */
    if (strstr(hotkey_str, "Slash")) *vk = VK_OEM_2;
    else if (strstr(hotkey_str, "F1")) *vk = VK_F1;
    else if (strstr(hotkey_str, "F2")) *vk = VK_F2;
    else if (strstr(hotkey_str, "F3")) *vk = VK_F3;
    else if (strstr(hotkey_str, "F4")) *vk = VK_F4;
    else if (strstr(hotkey_str, "F5")) *vk = VK_F5;
    else if (strstr(hotkey_str, "F6")) *vk = VK_F6;
    else if (strstr(hotkey_str, "F7")) *vk = VK_F7;
    else if (strstr(hotkey_str, "F8")) *vk = VK_F8;
    else if (strstr(hotkey_str, "F9")) *vk = VK_F9;
    else if (strstr(hotkey_str, "F10")) *vk = VK_F10;
    else if (strstr(hotkey_str, "F11")) *vk = VK_F11;
    else if (strstr(hotkey_str, "F12")) *vk = VK_F12;
    else if (strstr(hotkey_str, "Space")) *vk = VK_SPACE;
    else if (strstr(hotkey_str, "Enter")) *vk = VK_RETURN;
    else if (strstr(hotkey_str, "Escape")) *vk = VK_ESCAPE;
    else if (strstr(hotkey_str, "Enter")) *vk = VK_RETURN;

    /* Require at least one modifier and a recognized non-modifier key */
    if (*modifiers == 0 || *vk == 0) {
        return 0;
    }
    return 1;
}

static int init_overlay(void) {
    if (!ensure_original_image()) {
        MessageBoxA(NULL,
            "Could not find keymap.png or embedded fallback.",
            "Error", MB_OK);
        return 0;
    }

    int max_w, max_h;
    if (g_config.use_custom_size) {
        max_w = g_config.custom_width_px;
        max_h = g_config.custom_height_px;
    } else {
        max_w = (int)(1920 * g_config.scale);
        max_h = (int)(1080 * g_config.scale);
    }

    OverlayError result = load_overlay_mem(g_original_image, g_original_image_size,
                                           max_w, max_h, &g_overlay);
    if (result != OVERLAY_OK) {
        const char *error_msg = "Unknown error";
        switch (result) {
            case OVERLAY_ERROR_FILE_NOT_FOUND:
                error_msg = "Could not find keymap.png in any location"; break;
            case OVERLAY_ERROR_DECODE_FAILED:
                error_msg = "Could not decode image file"; break;
            case OVERLAY_ERROR_OUT_OF_MEMORY:
                error_msg = "Out of memory loading image"; break;
            case OVERLAY_ERROR_RESIZE_FAILED:
                error_msg = "Failed to resize image"; break;
            case OVERLAY_ERROR_NULL_PARAM:
                error_msg = "Internal error"; break;
        }

        char full_msg[512];
        snprintf(full_msg, sizeof(full_msg),
            "%s\n\nPlease place keymap.png in one of these locations:\n"
            "• assets\\ folder (before building)\n"
            "• Same directory as exe", error_msg);
        MessageBoxA(NULL, full_msg, "Error", MB_OK);
        return 0;
    }

    apply_effects(&g_overlay, g_config.opacity, g_config.invert);
    g_last_scale = g_config.scale;
    g_last_custom_width = g_config.custom_width_px;
    g_last_custom_height = g_config.custom_height_px;
    g_last_use_custom = g_config.use_custom_size;
    return 1;
}

static int create_bitmap(void) {
    BITMAPV5HEADER bi = {0};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = g_overlay.width;
    bi.bV5Height = -g_overlay.height; /* Top-down DIB */
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    g_screen_dc = GetDC(NULL);
    if (!g_screen_dc) return 0;
    
    g_mem_dc = CreateCompatibleDC(g_screen_dc);
    if (!g_mem_dc) return 0;
    
    g_bitmap = CreateDIBSection(g_screen_dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS,
                                &g_bitmap_bits, NULL, 0);
    if (!g_bitmap) return 0;
    
    memcpy(g_bitmap_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
    return 1;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *pk = (KBDLLHOOKSTRUCT *)lParam;
        int is_key_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        int is_key_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        
        /* Skip key repeat events - enhanced from working example */
        static DWORD last_time = 0;
        static UINT last_vk = 0;
        DWORD current_time = GetTickCount();
        
        if (pk && is_key_down && pk->vkCode == last_vk && (current_time - last_time) < 50) {
            /* Likely a key repeat - ignore to prevent multiple triggers */
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
                        if (!g_key_pressed) {
                            g_key_pressed = 1;
                            logger_log("Hotkey pressed (vk=%u mods=0x%04x)", (unsigned)pk->vkCode, mods);
                            PostMessage(g_hidden_window, MSG_SHOW_OVERLAY, 0, 0);
                        }
                    } else if (is_key_up) {
                        if (g_key_pressed) {
                            g_key_pressed = 0;
                            logger_log("Hotkey released (vk=%u)", (unsigned)pk->vkCode);
                            /* Hide on release only when auto-hide is enabled */
                            if (g_config.auto_hide > 0.0f) {
                                PostMessage(g_hidden_window, MSG_HIDE_OVERLAY, 0, 0);
                            }
                        }
                    }
                } else {
                    /* If modifiers no longer match while we thought key was pressed, hide */
                    if (g_key_pressed) {
                        g_key_pressed = 0;
                        logger_log("Hotkey modifiers changed - hiding overlay");
                        if (g_config.auto_hide > 0.0f) {
                            PostMessage(g_hidden_window, MSG_HIDE_OVERLAY, 0, 0);
                        }
                    }
                }
            }
        }
    }
    return CallNextHookEx(g_kb_hook, nCode, wParam, lParam);
}

static void show_overlay(void) {
    if (!g_window || g_visible) return;
    
    SelectObject(g_mem_dc, g_bitmap);
    
    /* Use selected monitor bounds */
    RECT mon = get_monitor_rect(g_config.monitor_index);
    int screen_w = mon.right - mon.left;
    int screen_h = mon.bottom - mon.top;

    /* Center overlay on chosen monitor */
    int x = mon.left + (screen_w - g_overlay.width) / 2 + g_config.position_x;
    int y = mon.top + screen_h - g_overlay.height - g_config.position_y;
    
    SetWindowPos(g_window, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    
    SIZE size = {g_overlay.width, g_overlay.height};
    POINT src = {0, 0};
    POINT dst = {x, y};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    
    UpdateLayeredWindow(g_window, g_screen_dc, &dst, &size, g_mem_dc, &src, 0, &bf, ULW_ALPHA);
    ShowWindow(g_window, SW_SHOWNOACTIVATE);
    g_visible = 1;
    
    /* Auto-hide timer when enabled */
    if (g_config.auto_hide > 0.0f) {
        SetTimer(g_hidden_window, 1, (UINT)(g_config.auto_hide * 1000), NULL);
    }
}

static void hide_overlay(void) {
    if (g_window && g_visible) {
        /* Kill auto-hide timer if active */
        KillTimer(g_hidden_window, 1);
        ShowWindow(g_window, SW_HIDE);
        g_visible = 0;
    }
}

static void toggle_overlay(void) {
    if (g_visible) hide_overlay();
    else show_overlay();
}

static void reload_overlay_if_needed(void) {
    if (fabsf(g_config.scale - g_last_scale) < 0.001f &&
        g_config.custom_width_px == g_last_custom_width &&
        g_config.custom_height_px == g_last_custom_height &&
        g_config.use_custom_size == g_last_use_custom) {
        return; /* No changes */
    }

    g_last_scale = g_config.scale;
    g_last_custom_width = g_config.custom_width_px;
    g_last_custom_height = g_config.custom_height_px;
    g_last_use_custom = g_config.use_custom_size;

    BOOL was_visible = g_visible;
    if (was_visible) hide_overlay();

    if (g_bitmap) { DeleteObject(g_bitmap); g_bitmap = NULL; }
    if (g_mem_dc) { DeleteDC(g_mem_dc); g_mem_dc = NULL; }
    if (g_screen_dc) { ReleaseDC(NULL, g_screen_dc); g_screen_dc = NULL; }
    free_overlay(&g_overlay);

    int max_w, max_h;
    if (g_config.use_custom_size) {
        max_w = g_config.custom_width_px;
        max_h = g_config.custom_height_px;
    } else {
        max_w = (int)(1920 * g_config.scale);
        max_h = (int)(1080 * g_config.scale);
    }

    OverlayError res = load_overlay_mem(g_original_image, g_original_image_size,
                                        max_w, max_h, &g_overlay);
    if (res == OVERLAY_OK) {
        apply_effects(&g_overlay, g_config.opacity, g_config.invert);
        if (create_bitmap() && was_visible) show_overlay();
    }
}

/* Enhanced tray menu matching macOS design */
static void show_tray_menu(void) {
    HMENU menu = CreatePopupMenu();
    
    /* Show Keymap toggle with state indicator */
    UINT showKeymapFlags = MF_STRING | (g_visible ? MF_CHECKED : 0);
    AppendMenuA(menu, showKeymapFlags, 101, "Show Keymap");
    
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    
    /* Scale submenu */
    HMENU scaleMenu = CreatePopupMenu();
    AppendMenuA(scaleMenu, MF_STRING | (fabsf(g_config.scale - 0.75f) < 0.001f ? MF_CHECKED : 0), 201, "75%");
    AppendMenuA(scaleMenu, MF_STRING | (fabsf(g_config.scale - 1.0f) < 0.001f ? MF_CHECKED : 0), 202, "100%");
    AppendMenuA(scaleMenu, MF_STRING | (fabsf(g_config.scale - 1.25f) < 0.001f ? MF_CHECKED : 0), 203, "125%");
    AppendMenuA(scaleMenu, MF_STRING | (fabsf(g_config.scale - 1.5f) < 0.001f ? MF_CHECKED : 0), 204, "150%");
    AppendMenuA(scaleMenu, MF_STRING, 205, "Fit Screen");
    AppendMenuA(menu, MF_STRING | MF_POPUP, (UINT_PTR)scaleMenu, "Scale");
    
    /* Opacity submenu */
    HMENU opacityMenu = CreatePopupMenu();
    AppendMenuA(opacityMenu, MF_STRING | (fabsf(g_config.opacity - 0.5f) < 0.001f ? MF_CHECKED : 0), 301, "50%");
    AppendMenuA(opacityMenu, MF_STRING | (fabsf(g_config.opacity - 0.7f) < 0.001f ? MF_CHECKED : 0), 302, "70%");
    AppendMenuA(opacityMenu, MF_STRING | (fabsf(g_config.opacity - 0.85f) < 0.001f ? MF_CHECKED : 0), 303, "85%");
    AppendMenuA(opacityMenu, MF_STRING | (fabsf(g_config.opacity - 1.0f) < 0.001f ? MF_CHECKED : 0), 304, "100%");
    AppendMenuA(menu, MF_STRING | MF_POPUP, (UINT_PTR)opacityMenu, "Opacity");
    
    /* Auto-hide submenu */
    HMENU autoHideMenu = CreatePopupMenu();
    AppendMenuA(autoHideMenu, MF_STRING | (g_config.auto_hide == 0.0f ? MF_CHECKED : 0), 401, "Off");
    AppendMenuA(autoHideMenu, MF_STRING | (fabsf(g_config.auto_hide - 0.8f) < 0.001f ? MF_CHECKED : 0), 402, "0.8s");
    AppendMenuA(autoHideMenu, MF_STRING | (fabsf(g_config.auto_hide - 2.0f) < 0.001f ? MF_CHECKED : 0), 403, "2.0s");
    AppendMenuA(autoHideMenu, MF_STRING, 404, "Custom...");
    AppendMenuA(menu, MF_STRING | MF_POPUP, (UINT_PTR)autoHideMenu, "Auto-hide");

    /* Monitor submenu */
    HMENU monitorMenu = CreatePopupMenu();
    int mcount = get_monitor_count();
    for (int i = 0; i < mcount; i++) {
        char label[32];
        snprintf(label, sizeof(label), "Monitor %d", i + 1);
        UINT flags = MF_STRING | (i == g_config.monitor_index ? MF_CHECKED : 0);
        AppendMenuA(monitorMenu, flags, 500 + i, label);
    }
    AppendMenuA(menu, MF_STRING | MF_POPUP, (UINT_PTR)monitorMenu, "Monitor");
    
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    
    /* Preview Keymap */
    AppendMenuA(menu, MF_STRING, 102, "Preview Keymap");
    
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    
    /* Preferences */
    AppendMenuA(menu, MF_STRING, ID_PREFS_OPEN, "Preferences...");
    AppendMenuA(menu, MF_STRING, 3, "Quit");
    
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(g_hidden_window);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN, p.x, p.y, 0, g_hidden_window, NULL);
    DestroyMenu(menu);
}

/* Preferences window implementation (simple): editable hotkey and opacity (0-100) */
static LRESULT CALLBACK PrefsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_HSCROLL: {
        /* Handle slider changes for real-time feedback */
        HWND hSlider = (HWND)lParam;
        if (hSlider == g_scale_slider) {
            int pos = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);
            g_config.scale = pos / 100.0f;
            update_scale_label();
            /* Apply scale immediately for real-time preview */
            reload_overlay_if_needed();
            return 0;
        } else if (hSlider == g_opacity_slider) {
            int pos = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);
            g_config.opacity = pos / 100.0f;
            update_opacity_label();
            /* Apply opacity immediately for real-time preview */
            if (g_visible) {
                apply_effects(&g_overlay, g_config.opacity, g_config.invert);
            }
            return 0;
        } else if (hSlider == g_autohide_slider) {
            int pos = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);
            g_config.auto_hide = (pos == 0) ? 0.0f : (pos / 10.0f);
            update_autohide_label();
            return 0;
        }
        break;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case IDC_PREFS_OK: {
                char buf[128];
                GetWindowTextA(g_prefs_hotkey_edit, buf, sizeof(buf));
                /* Read candidate hotkey from input */
                UINT modifiers = 0, vk = 0;
                if (!parse_hotkey(buf, &modifiers, &vk)) {
                    MessageBoxA(hwnd,
                        "Invalid hotkey. Use at least one modifier (Ctrl/Alt/Shift/Win) and a non-modifier key (e.g. Slash, F1, Space).",
                        "Invalid Hotkey", MB_OK | MB_ICONERROR);
                    return 0;
                }
                /* Save hotkey */
                strncpy(g_config.hotkey, buf, sizeof(g_config.hotkey) - 1);
                g_config.hotkey[sizeof(g_config.hotkey)-1] = '\0';
                /* Update hook mapping immediately (safe: parsing already succeeded) */
                g_hook_modifiers = modifiers;
                g_hook_vk = vk;
                
                /* Sliders have already updated g_config values in real-time */
                /* Just persist the configuration */
                save_config(&g_config, NULL);
                
                /* Ensure final state is applied */
                if (g_visible) {
                    apply_effects(&g_overlay, g_config.opacity, g_config.invert);
                }
                reload_overlay_if_needed();
                
                DestroyWindow(hwnd);
                g_prefs_window = NULL;
                return 0;
            }
            case IDC_PREFS_CANCEL:
                /* Cancel pressed - restore original values */
                /* Note: For full implementation, we should save original values when window opens
                   and restore them here. For now, just close the window. */
                DestroyWindow(hwnd);
                g_prefs_window = NULL;
                return 0;
            }
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        g_prefs_window = NULL;
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

static void update_scale_label(void) {
    char buf[32];
    int scale_pct = (int)roundf(g_config.scale * 100.0f);
    snprintf(buf, sizeof(buf), "Scale: %d%%", scale_pct);
    SetWindowTextA(g_scale_label, buf);
}

static void update_opacity_label(void) {
    char buf[32];
    int opacity_pct = (int)roundf(g_config.opacity * 100.0f);
    snprintf(buf, sizeof(buf), "Opacity: %d%%", opacity_pct);
    SetWindowTextA(g_opacity_label, buf);
}

static void update_autohide_label(void) {
    char buf[32];
    if (g_config.auto_hide == 0.0f) {
        snprintf(buf, sizeof(buf), "Auto-hide: Off");
    } else {
        snprintf(buf, sizeof(buf), "Auto-hide: %.1fs", g_config.auto_hide);
    }
    SetWindowTextA(g_autohide_label, buf);
}

static void open_prefs_window(void) {
    if (g_prefs_window) {
        SetForegroundWindow(g_prefs_window);
        return;
    }

    HINSTANCE hInst = GetModuleHandle(NULL);
    const char *cls = "KLO_Prefs_Class";
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = PrefsWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    RegisterClassA(&wc);

    int w = 380, h = 380;
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    int x = (sx - w) / 2;
    int y = (sy - h) / 2;

    g_prefs_window = CreateWindowExA(WS_EX_TOOLWINDOW, cls, "Preferences",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, NULL, NULL, hInst, NULL);

    int yPos = 20;

    /* Hotkey Group */
    CreateWindowExA(WS_EX_DLGMODALFRAME, "BUTTON", "Hotkey", 
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    15, yPos, 340, 60, g_prefs_window, NULL, hInst, NULL);
    
    CreateWindowExA(0, "STATIC", "Global hotkey:", WS_CHILD | WS_VISIBLE,
                    25, yPos + 25, 80, 20, g_prefs_window, NULL, hInst, NULL);
    g_prefs_hotkey_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.hotkey,
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    115, yPos + 23, 230, 22, g_prefs_window, (HMENU)IDC_PREF_HOTKEY, hInst, NULL);
    
    yPos += 80;

    /* Scale Group */
    CreateWindowExA(WS_EX_DLGMODALFRAME, "BUTTON", "Scale", 
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    15, yPos, 340, 60, g_prefs_window, NULL, hInst, NULL);
    
    g_scale_label = CreateWindowExA(0, "STATIC", "Scale: 100%", WS_CHILD | WS_VISIBLE,
                    25, yPos + 20, 120, 20, g_prefs_window, (HMENU)IDC_SCALE_LABEL, hInst, NULL);
    
    /* Scale slider: 50% to 200% (values 50-200) */
    g_scale_slider = CreateWindowExA(0, TRACKBAR_CLASS, NULL,
                    WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                    150, yPos + 18, 190, 30, g_prefs_window, (HMENU)IDC_SCALE_SLIDER, hInst, NULL);
    SendMessage(g_scale_slider, TBM_SETRANGE, TRUE, MAKELONG(50, 200));
    SendMessage(g_scale_slider, TBM_SETTICFREQ, 25, 0);
    SendMessage(g_scale_slider, TBM_SETPOS, TRUE, (int)(g_config.scale * 100));
    
    yPos += 80;

    /* Opacity Group */
    CreateWindowExA(WS_EX_DLGMODALFRAME, "BUTTON", "Opacity", 
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    15, yPos, 340, 60, g_prefs_window, NULL, hInst, NULL);
    
    g_opacity_label = CreateWindowExA(0, "STATIC", "Opacity: 85%", WS_CHILD | WS_VISIBLE,
                    25, yPos + 20, 120, 20, g_prefs_window, (HMENU)IDC_OPACITY_LABEL, hInst, NULL);
    
    /* Opacity slider: 10% to 100% (values 10-100) */
    g_opacity_slider = CreateWindowExA(0, TRACKBAR_CLASS, NULL,
                    WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                    150, yPos + 18, 190, 30, g_prefs_window, (HMENU)IDC_OPACITY_SLIDER, hInst, NULL);
    SendMessage(g_opacity_slider, TBM_SETRANGE, TRUE, MAKELONG(10, 100));
    SendMessage(g_opacity_slider, TBM_SETTICFREQ, 10, 0);
    SendMessage(g_opacity_slider, TBM_SETPOS, TRUE, (int)(g_config.opacity * 100));
    
    yPos += 80;

    /* Auto-hide Group */
    CreateWindowExA(WS_EX_DLGMODALFRAME, "BUTTON", "Auto-hide", 
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    15, yPos, 340, 60, g_prefs_window, NULL, hInst, NULL);
    
    g_autohide_label = CreateWindowExA(0, "STATIC", "Auto-hide: Off", WS_CHILD | WS_VISIBLE,
                    25, yPos + 20, 120, 20, g_prefs_window, (HMENU)IDC_AUTOHIDE_LABEL, hInst, NULL);
    
    /* Auto-hide slider: Off, 0.5s to 5.0s (values 0-50, where 0=off, 1-50 = 0.1s * value) */
    g_autohide_slider = CreateWindowExA(0, TRACKBAR_CLASS, NULL,
                    WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                    150, yPos + 18, 190, 30, g_prefs_window, (HMENU)IDC_AUTOHIDE_SLIDER, hInst, NULL);
    SendMessage(g_autohide_slider, TBM_SETRANGE, TRUE, MAKELONG(0, 50));
    SendMessage(g_autohide_slider, TBM_SETTICFREQ, 10, 0);
    int autohide_pos = (g_config.auto_hide == 0.0f) ? 0 : (int)(g_config.auto_hide * 10);
    SendMessage(g_autohide_slider, TBM_SETPOS, TRUE, autohide_pos);
    
    yPos += 80;

    /* Update all labels with current values */
    update_scale_label();
    update_opacity_label();
    update_autohide_label();

    /* OK and Cancel buttons */
    CreateWindowExA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    220, yPos + 10, 60, 28, g_prefs_window, (HMENU)IDC_PREFS_OK, hInst, NULL);
    CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    290, yPos + 10, 60, 28, g_prefs_window, (HMENU)IDC_PREFS_CANCEL, hInst, NULL);

    ShowWindow(g_prefs_window, SW_SHOW);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAY:
        if (lParam == WM_RBUTTONUP) show_tray_menu();
        break;
        
    case MSG_SHOW_OVERLAY:
        show_overlay();
        break;
    case MSG_HIDE_OVERLAY:
        hide_overlay();
        break;
        
    case WM_TIMER:
        if (wParam == 1) {
            /* Auto-hide timer expired */
            hide_overlay();
        } else if (wParam == 2) {
            /* Preview timer expired */
            KillTimer(g_hidden_window, 2);
            if (g_visible) {
                hide_overlay();
            }
        }
        break;
        
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        /* Show Keymap toggle */
        case 101:
            toggle_overlay();
            break;
        /* Preview Keymap */
        case 102: {
            show_overlay();
            /* Auto-hide preview after 3 seconds */
            SetTimer(g_hidden_window, 2, 3000, NULL);
            break;
        }
        /* Scale options */
        case 201: /* 75% */
            g_config.scale = 0.75f;
            g_config.use_custom_size = 0;
            save_config(&g_config, NULL);
            reload_overlay_if_needed();
            break;
        case 202: /* 100% */
            g_config.scale = 1.0f;
            g_config.use_custom_size = 0;
            save_config(&g_config, NULL);
            reload_overlay_if_needed();
            break;
        case 203: /* 125% */
            g_config.scale = 1.25f;
            g_config.use_custom_size = 0;
            save_config(&g_config, NULL);
            reload_overlay_if_needed();
            break;
        case 204: /* 150% */
            g_config.scale = 1.5f;
            g_config.use_custom_size = 0;
            save_config(&g_config, NULL);
            reload_overlay_if_needed();
            break;
        case 205: /* Fit Screen */ {
            /* Calculate scale to fit 80% of primary monitor width */
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            float targetWidth = screenWidth * 0.8f;
            if (g_overlay.width > 0) {
                g_config.scale = targetWidth / g_overlay.width;
                g_config.use_custom_size = 0;
                save_config(&g_config, NULL);
                reload_overlay_if_needed();
            }
            break;
        }
        /* Opacity options */
        case 301: /* 50% */
            g_config.opacity = 0.5f;
            save_config(&g_config, NULL);
            apply_effects(&g_overlay, g_config.opacity, g_config.invert);
            memcpy(g_bitmap_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
            if (g_visible) show_overlay();
            break;
        case 302: /* 70% */
            g_config.opacity = 0.7f;
            save_config(&g_config, NULL);
            apply_effects(&g_overlay, g_config.opacity, g_config.invert);
            memcpy(g_bitmap_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
            if (g_visible) show_overlay();
            break;
        case 303: /* 85% */
            g_config.opacity = 0.85f;
            save_config(&g_config, NULL);
            apply_effects(&g_overlay, g_config.opacity, g_config.invert);
            memcpy(g_bitmap_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
            if (g_visible) show_overlay();
            break;
        case 304: /* 100% */
            g_config.opacity = 1.0f;
            save_config(&g_config, NULL);
            apply_effects(&g_overlay, g_config.opacity, g_config.invert);
            memcpy(g_bitmap_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
            if (g_visible) show_overlay();
            break;
        /* Auto-hide options */
        case 401: /* Off */
            g_config.auto_hide = 0.0f;
            save_config(&g_config, NULL);
            break;
        case 402: /* 0.8s */
            g_config.auto_hide = 0.8f;
            save_config(&g_config, NULL);
            break;
        case 403: /* 2.0s */
            g_config.auto_hide = 2.0f;
            save_config(&g_config, NULL);
            break;
        case 404: /* Custom... */
            /* Open preferences for custom auto-hide setting */
            open_prefs_window();
            break;
        case ID_PREFS_OPEN:
            open_prefs_window();
            break;
        case 3: /* Quit */
            PostQuitMessage(0);
            break;
        default:
            if (LOWORD(wParam) >= 500 && LOWORD(wParam) < 500 + get_monitor_count()) {
                g_config.monitor_index = LOWORD(wParam) - 500;
                save_config(&g_config, NULL);
                if (g_visible) show_overlay();
            }
            break;
        }
        break;
        
    case WM_DESTROY:
        cleanup_resources();
        PostQuitMessage(0);
        break;
        
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShow) {
    /* Initialize common controls for trackbar sliders */
    InitCommonControls();
    
    /* Per-monitor DPI awareness for crisp rendering (from working example) */
    typedef BOOL (WINAPI *SetPDACTX)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        SetPDACTX setPDACtx = (SetPDACTX)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setPDACtx) {
            /* Use per-monitor aware V2 for best multi-monitor support */
            setPDACtx((DPI_AWARENESS_CONTEXT)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        }
    }
    
    g_config = get_default_config();
    /* Load persisted config if present (overrides defaults) */
    load_config(&g_config, NULL);

    /* Initialize logger early for parity with macOS */
    logger_init();
    logger_log("KbdLayoutOverlay (Windows) starting up");

    if (!init_overlay() || !create_bitmap()) {
        cleanup_resources();
        logger_log("Startup failed: overlay/init error");
        logger_close();
        return 1;
    }
    
    /* Register window class */
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "KbdLayoutOverlay";
    RegisterClassA(&wc);
    
    /* Create hidden message window */
    g_hidden_window = CreateWindowA("KbdLayoutOverlay", "", 0, 0, 0, 0, 0, 
                                    HWND_MESSAGE, NULL, hInst, NULL);
    
    /* Create overlay window on selected monitor */
    RECT mon = get_monitor_rect(g_config.monitor_index);
    g_window = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               "KbdLayoutOverlay", "", WS_POPUP,
                               mon.left, mon.top,
                               mon.right - mon.left,
                               mon.bottom - mon.top,
                               NULL, NULL, hInst, NULL);
    
    if (!g_window || !g_hidden_window) {
        cleanup_resources();
        return 1;
    }
    
    /* Register global low-level keyboard hook for press+release detection */
    {
        UINT modifiers = 0, vk = 0;
        if (!parse_hotkey(g_config.hotkey, &modifiers, &vk)) {
            /* Keep defaults and notify user but continue running */
            MessageBoxA(NULL,
                "Configured hotkey is invalid. Using default hotkey. Update Preferences to set a valid hotkey.",
                "Hotkey Warning", MB_OK | MB_ICONWARNING);
        } else {
            g_hook_modifiers = modifiers;
            g_hook_vk = vk;
        }
    }
    g_kb_hook = SetWindowsHookExA(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (!g_kb_hook) {
        DWORD error = GetLastError();
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
            "Failed to install keyboard hook (Error: %lu)\n\n"
            "Hotkey may not be detected while other apps are focused.",
            error);
        MessageBoxA(NULL, error_msg, "Hotkey Hook Failed", MB_OK | MB_ICONWARNING);
    }
    
    /* Create tray icon */
    NOTIFYICONDATAA nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hidden_window;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(nid.szTip, "Keyboard Layout Overlay");
    Shell_NotifyIconA(NIM_ADD, &nid);
    
    /* Message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    /* Cleanup */
    Shell_NotifyIconA(NIM_DELETE, &nid);
    if (g_kb_hook) UnhookWindowsHookEx(g_kb_hook);
    cleanup_resources();

    logger_log("KbdLayoutOverlay (Windows) exiting");
    logger_close();
    
    return 0;
}
