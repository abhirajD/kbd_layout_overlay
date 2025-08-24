#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

/* Control IDs */
#define ID_PREFS_OPEN 8
#define IDC_PREFS_OK 101
#define IDC_PREFS_CANCEL 102
#define IDC_PREF_HOTKEY 201
#define IDC_PREF_OPACITY 202
#define IDC_USE_CUSTOM_SIZE 203
#define IDC_CUSTOM_WIDTH 204
#define IDC_CUSTOM_HEIGHT 205

static void cleanup_resources(void) {
    if (g_bitmap) DeleteObject(g_bitmap);
    if (g_mem_dc) DeleteDC(g_mem_dc);
    if (g_screen_dc) ReleaseDC(NULL, g_screen_dc);
    free_overlay(&g_overlay);
}

/* Get virtual desktop bounds (multi-monitor support from working example) */
static RECT get_virtual_desktop(void) {
    RECT r;
    r.left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.right  = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.bottom = r.top  + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r;
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
    /* Determine overlay dimensions */
    int max_w, max_h;
    if (g_config.use_custom_size) {
        /* Use custom pixel dimensions */
        max_w = g_config.custom_width_px;
        max_h = g_config.custom_height_px;
    } else {
        /* Apply configurable scaling to max dimensions */
        max_w = (int)(1920 * g_config.scale);
        max_h = (int)(1080 * g_config.scale);
    }
    
    /* Try multiple locations for keymap.png */
    const char *search_paths[] = {
        "keymap.png",           // Current directory
        "assets\\keymap.png",   // Assets folder
        "..\\assets\\keymap.png", // Assets folder (relative)
        NULL
    };
    
    OverlayError result = OVERLAY_ERROR_FILE_NOT_FOUND;
    for (int i = 0; search_paths[i] != NULL; i++) {
        result = load_overlay(search_paths[i], max_w, max_h, &g_overlay);
        if (result == OVERLAY_OK) {
            break;
        }
    }
    
    if (result != OVERLAY_OK) {
        /* Try embedded fallback */
        int size;
        const unsigned char *data = get_default_keymap(&size);
        if (data && size > 0) {
            result = load_overlay_mem(data, size, max_w, max_h, &g_overlay);
        }
    }
    
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
        }
        
        char full_msg[512];
        snprintf(full_msg, sizeof(full_msg),
            "%s\n\nPlease place keymap.png in one of these locations:\n"
            "• assets\\ folder (before building)\n"
            "• Same directory as exe", error_msg);
        MessageBoxA(NULL, full_msg, "Error", MB_OK);
        return 0;
    }
    
    /* Apply effects */
    apply_effects(&g_overlay, g_config.opacity, g_config.invert);
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
                            /* In non-persistent mode, hide on release; persistent mode ignores release */
                            if (!g_config.persistent) {
                                PostMessage(g_hidden_window, MSG_HIDE_OVERLAY, 0, 0);
                            }
                        }
                    }
                } else {
                    /* If modifiers no longer match while we thought key was pressed, hide */
                    if (g_key_pressed) {
                        g_key_pressed = 0;
                        logger_log("Hotkey modifiers changed - hiding overlay");
                        if (!g_config.persistent) {
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
    
    /* Use virtual desktop bounds for multi-monitor support */
    RECT vdesktop = get_virtual_desktop();
    int screen_w = vdesktop.right - vdesktop.left;
    int screen_h = vdesktop.bottom - vdesktop.top;
    
    /* Center overlay on virtual desktop */
    int x = vdesktop.left + (screen_w - g_overlay.width) / 2 + g_config.position_x;
    int y = vdesktop.top + screen_h - g_overlay.height - g_config.position_y;
    
    SetWindowPos(g_window, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    
    SIZE size = {g_overlay.width, g_overlay.height};
    POINT src = {0, 0};
    POINT dst = {x, y};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    
    UpdateLayeredWindow(g_window, g_screen_dc, &dst, &size, g_mem_dc, &src, 0, &bf, ULW_ALPHA);
    ShowWindow(g_window, SW_SHOWNOACTIVATE);
    g_visible = 1;
    
    /* Auto-hide timer for non-persistent mode (matching macOS behavior) */
    if (!g_config.persistent) {
        SetTimer(g_hidden_window, 1, 800, NULL);  // 0.8 seconds like macOS
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

static void reload_overlay_with_scale(void) {
    BOOL was_visible = g_visible;
    if (was_visible) hide_overlay();
    
    /* Clean up current resources */
    cleanup_resources();
    
    /* Reinitialize with new scale */
    if (init_overlay() && create_bitmap()) {
        if (was_visible) show_overlay();
    }
}

static void show_tray_menu(void) {
    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING | (g_config.persistent ? MF_CHECKED : 0), 1, "Persistent mode");
    AppendMenuA(menu, MF_STRING | (g_config.invert ? MF_CHECKED : 0), 2, "Invert colors");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING | (g_config.scale == 0.5f ? MF_CHECKED : 0), 4, "Size: 50%");
    AppendMenuA(menu, MF_STRING | (g_config.scale == 0.75f ? MF_CHECKED : 0), 5, "Size: 75%");
    AppendMenuA(menu, MF_STRING | (g_config.scale == 1.0f ? MF_CHECKED : 0), 6, "Size: 100%");
    AppendMenuA(menu, MF_STRING | (g_config.scale == 1.5f ? MF_CHECKED : 0), 7, "Size: 150%");
    /* Preferences */
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, ID_PREFS_OPEN, "Preferences...");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
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
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case IDC_USE_CUSTOM_SIZE: {
                BOOL checked = (SendMessage(g_prefs_custom_size_check, BM_GETCHECK, 0, 0) == BST_CHECKED);
                EnableWindow(g_prefs_custom_width_edit, checked);
                EnableWindow(g_prefs_custom_height_edit, checked);
                return 0;
            }
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
                /* Opacity */
                char opbuf[32];
                GetWindowTextA(g_prefs_opacity_edit, opbuf, sizeof(opbuf));
                float op = (float)atof(opbuf) / 100.0f;
                if (op < 0.0f) op = 0.0f;
                if (op > 1.0f) op = 1.0f;
                g_config.opacity = op;
                
                /* Custom sizing */
                g_config.use_custom_size = (SendMessage(g_prefs_custom_size_check, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                
                if (g_config.use_custom_size) {
                    char widthbuf[32], heightbuf[32];
                    GetWindowTextA(g_prefs_custom_width_edit, widthbuf, sizeof(widthbuf));
                    GetWindowTextA(g_prefs_custom_height_edit, heightbuf, sizeof(heightbuf));
                    
                    int width = atoi(widthbuf);
                    int height = atoi(heightbuf);
                    
                    /* Validate dimensions (reasonable limits) */
                    if (width < 100 || width > 2000 || height < 100 || height > 2000) {
                        MessageBoxA(hwnd,
                            "Width and height must be between 100 and 2000 pixels.",
                            "Invalid Size", MB_OK | MB_ICONERROR);
                        return 0;
                    }
                    
                    g_config.custom_width_px = width;
                    g_config.custom_height_px = height;
                }
                
                /* Persist and apply */
                save_config(&g_config, NULL);
                apply_effects(&g_overlay, g_config.opacity, g_config.invert);
                /* Reload overlay if custom sizing changed */
                reload_overlay_with_scale();
                DestroyWindow(hwnd);
                g_prefs_window = NULL;
                return 0;
            }
            case IDC_PREFS_CANCEL:
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
    RegisterClassA(&wc);

    int w = 360, h = 220;
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    int x = (sx - w) / 2;
    int y = (sy - h) / 2;

    g_prefs_window = CreateWindowExA(WS_EX_TOOLWINDOW, cls, "Preferences",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, NULL, NULL, hInst, NULL);

    /* Hotkey label */
    CreateWindowExA(0, "STATIC", "Hotkey:", WS_CHILD | WS_VISIBLE,
                    12, 24, 60, 20, g_prefs_window, NULL, hInst, NULL);
    /* Hotkey edit */
    g_prefs_hotkey_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_config.hotkey,
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    80, 22, 260, 22, g_prefs_window, (HMENU)IDC_PREF_HOTKEY, hInst, NULL);

    /* Opacity label */
    CreateWindowExA(0, "STATIC", "Opacity (0-100):", WS_CHILD | WS_VISIBLE,
                    12, 60, 120, 20, g_prefs_window, NULL, hInst, NULL);
    char opbuf[32];
    snprintf(opbuf, sizeof(opbuf), "%d", (int)roundf(g_config.opacity * 100.0f));
    g_prefs_opacity_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", opbuf,
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    140, 58, 80, 22, g_prefs_window, (HMENU)IDC_PREF_OPACITY, hInst, NULL);

    /* Custom size controls */
    g_prefs_custom_size_check = CreateWindowExA(0, "BUTTON", "Use custom size", 
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                    12, 95, 140, 20, g_prefs_window, (HMENU)IDC_USE_CUSTOM_SIZE, hInst, NULL);
    
    CreateWindowExA(0, "STATIC", "Width (px):", WS_CHILD | WS_VISIBLE,
                    12, 125, 80, 20, g_prefs_window, NULL, hInst, NULL);
    char widthbuf[16];
    snprintf(widthbuf, sizeof(widthbuf), "%d", g_config.custom_width_px);
    g_prefs_custom_width_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", widthbuf,
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                    100, 123, 80, 22, g_prefs_window, (HMENU)IDC_CUSTOM_WIDTH, hInst, NULL);
    
    CreateWindowExA(0, "STATIC", "Height (px):", WS_CHILD | WS_VISIBLE,
                    190, 125, 80, 20, g_prefs_window, NULL, hInst, NULL);
    char heightbuf[16];
    snprintf(heightbuf, sizeof(heightbuf), "%d", g_config.custom_height_px);
    g_prefs_custom_height_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", heightbuf,
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                    280, 123, 80, 22, g_prefs_window, (HMENU)IDC_CUSTOM_HEIGHT, hInst, NULL);

    /* Set checkbox state and enable/disable fields based on config */
    SendMessage(g_prefs_custom_size_check, BM_SETCHECK, g_config.use_custom_size ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(g_prefs_custom_width_edit, g_config.use_custom_size);
    EnableWindow(g_prefs_custom_height_edit, g_config.use_custom_size);

    /* OK button */
    CreateWindowExA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    220, 170, 60, 24, g_prefs_window, (HMENU)IDC_PREFS_OK, hInst, NULL);
    /* Cancel button */
    CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    290, 170, 60, 24, g_prefs_window, (HMENU)IDC_PREFS_CANCEL, hInst, NULL);

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
        }
        break;
        
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1: /* Toggle persistent */
            g_config.persistent = !g_config.persistent;
            /* Persist change */
            save_config(&g_config, NULL);
            if (g_config.persistent && g_visible) hide_overlay();
            break;
        case 2: /* Toggle invert */
            g_config.invert = !g_config.invert;
            /* Persist change */
            save_config(&g_config, NULL);
            apply_effects(&g_overlay, g_config.opacity, g_config.invert);
            memcpy(g_bitmap_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
            if (g_visible) show_overlay();
            break;
        case 3: /* Quit */
            PostQuitMessage(0);
            break;
        case 4: /* Size 50% */
            g_config.scale = 0.5f;
            /* Persist change */
            save_config(&g_config, NULL);
            reload_overlay_with_scale();
            break;
        case 5: /* Size 75% */
            g_config.scale = 0.75f;
            /* Persist change */
            save_config(&g_config, NULL);
            reload_overlay_with_scale();
            break;
        case 6: /* Size 100% */
            g_config.scale = 1.0f;
            /* Persist change */
            save_config(&g_config, NULL);
            reload_overlay_with_scale();
            break;
        case 7: /* Size 150% */
            g_config.scale = 1.5f;
            /* Persist change */
            save_config(&g_config, NULL);
            reload_overlay_with_scale();
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
    
    /* Create overlay window spanning virtual desktop */
    RECT vdesktop = get_virtual_desktop();
    g_window = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               "KbdLayoutOverlay", "", WS_POPUP,
                               vdesktop.left, vdesktop.top, 
                               vdesktop.right - vdesktop.left, 
                               vdesktop.bottom - vdesktop.top,
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
