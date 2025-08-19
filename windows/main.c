#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include "../shared/config.h"
#include "../shared/overlay.h"

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
    
    if (strstr(hotkey_str, "Slash")) *vk = VK_OEM_2;
}

static int init_overlay(void) {
    /* Try to load keymap.png first */
    if (load_overlay("keymap.png", 1920, 1080, &g_overlay) != 0) {
        /* Fallback to embedded default */
        int size;
        const unsigned char *data = get_default_keymap(&size);
        if (load_overlay_mem(data, size, 1920, 1080, &g_overlay) != 0) {
            MessageBoxA(NULL, "Failed to load overlay image", "Error", MB_OK);
            return 0;
        }
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
    
    /* Center on primary monitor */
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - g_overlay.width) / 2;
    int y = screen_h - g_overlay.height - 100; /* 100px from bottom */
    
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

static void show_tray_menu(void) {
    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING | (g_config.persistent ? MF_CHECKED : 0), 1, "Persistent mode");
    AppendMenuA(menu, MF_STRING | (g_config.invert ? MF_CHECKED : 0), 2, "Invert colors");
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
    RegisterHotKey(g_hidden_window, HOTKEY_ID, modifiers, vk);
    
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