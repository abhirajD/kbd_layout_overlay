#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "overlay.h"

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

static void cleanup_resources(void) {
    if (g_bitmap) DeleteObject(g_bitmap);
    if (g_mem_dc) DeleteDC(g_mem_dc);
    if (g_screen_dc) ReleaseDC(NULL, g_screen_dc);
    free_overlay(&g_overlay);
}

static void parse_hotkey(const char *hotkey_str, UINT *modifiers, UINT *vk) {
    *modifiers = 0;
    *vk = VK_OEM_2; /* Default to '/' key */
    
    if (strstr(hotkey_str, "Ctrl")) *modifiers |= MOD_CONTROL;
    if (strstr(hotkey_str, "Alt")) *modifiers |= MOD_ALT;
    if (strstr(hotkey_str, "Shift")) *modifiers |= MOD_SHIFT;
    if (strstr(hotkey_str, "Win")) *modifiers |= MOD_WIN;
    
    /* Support more keys */
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
}

static int init_overlay(void) {
    /* Apply configurable scaling to max dimensions */
    int max_w = (int)(1920 * g_config.scale);
    int max_h = (int)(1080 * g_config.scale);
    
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

static void show_overlay(void) {
    if (!g_window || g_visible) return;
    
    SelectObject(g_mem_dc, g_bitmap);
    
    /* Apply configurable positioning */
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - g_overlay.width) / 2 + g_config.position_x;
    int y = screen_h - g_overlay.height - g_config.position_y;
    
    SetWindowPos(g_window, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    
    SIZE size = {g_overlay.width, g_overlay.height};
    POINT src = {0, 0};
    POINT dst = {0, 0};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    
    UpdateLayeredWindow(g_window, g_screen_dc, &dst, &size, g_mem_dc, &src, 0, &bf, ULW_ALPHA);
    ShowWindow(g_window, SW_SHOW);
    g_visible = 1;
}

static void hide_overlay(void) {
    if (g_window && g_visible) {
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
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, 3, "Quit");
    
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(g_hidden_window);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN, p.x, p.y, 0, g_hidden_window, NULL);
    DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAY:
        if (lParam == WM_RBUTTONUP) show_tray_menu();
        break;
        
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) toggle_overlay();
        break;
        
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1: /* Toggle persistent */
            g_config.persistent = !g_config.persistent;
            if (g_config.persistent && g_visible) hide_overlay();
            break;
        case 2: /* Toggle invert */
            g_config.invert = !g_config.invert;
            apply_effects(&g_overlay, g_config.opacity, g_config.invert);
            memcpy(g_bitmap_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
            if (g_visible) show_overlay();
            break;
        case 3: /* Quit */
            PostQuitMessage(0);
            break;
        case 4: /* Size 50% */
            g_config.scale = 0.5f;
            reload_overlay_with_scale();
            break;
        case 5: /* Size 75% */
            g_config.scale = 0.75f;
            reload_overlay_with_scale();
            break;
        case 6: /* Size 100% */
            g_config.scale = 1.0f;
            reload_overlay_with_scale();
            break;
        case 7: /* Size 150% */
            g_config.scale = 1.5f;
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
    g_config = get_default_config();
    
    if (!init_overlay() || !create_bitmap()) {
        cleanup_resources();
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
    
    /* Create overlay window */
    g_window = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                               "KbdLayoutOverlay", "", WS_POPUP,
                               0, 0, g_overlay.width, g_overlay.height,
                               NULL, NULL, hInst, NULL);
    
    if (!g_window || !g_hidden_window) {
        cleanup_resources();
        return 1;
    }
    
    /* Register hotkey */
    UINT modifiers, vk;
    parse_hotkey(g_config.hotkey, &modifiers, &vk);
    if (!RegisterHotKey(g_hidden_window, HOTKEY_ID, modifiers, vk)) {
        DWORD error = GetLastError();
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
            "Failed to register global hotkey '%s' (Error: %lu)\n\n"
            "The hotkey may already be in use by another application.",
            g_config.hotkey, error);
        MessageBoxA(NULL, error_msg, "Hotkey Registration Failed", MB_OK | MB_ICONWARNING);
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
    UnregisterHotKey(g_hidden_window, HOTKEY_ID);
    cleanup_resources();
    
    return 0;
}
