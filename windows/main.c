#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "../shared/config.h"
#include "../shared/overlay.h"

static Overlay g_overlay;
static HBITMAP g_bitmap;
static void *g_bits;
static HWND g_hwnd;
static Config g_cfg;
static NOTIFYICONDATAA g_nid;
static char g_cfg_path[MAX_PATH] = "config.cfg";
#define WM_TRAY (WM_APP + 1)

static void set_autostart(int enable) {
    HKEY key;
    if (RegCreateKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0,
            KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
        if (enable) {
            char path[MAX_PATH];
            GetModuleFileNameA(NULL, path, MAX_PATH);
            RegSetValueExA(key, "kbd_layout_overlay", 0, REG_SZ,
                (const BYTE *)path, (DWORD)(strlen(path) + 1));
        } else {
            RegDeleteValueA(key, "kbd_layout_overlay");
        }
        RegCloseKey(key);
    }
}

static int init_bitmap(void) {
    const char *path = g_cfg.overlay_path[0] ? g_cfg.overlay_path : "keymap.png";
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (load_overlay_image(path, screen_w, screen_h, &g_overlay) != 0) {
        char msg[256];
        sprintf(msg, "Failed to load %s", path);
        MessageBoxA(NULL, msg, "Error", MB_OK);
        return 0;
    }
    apply_opacity_inversion(&g_overlay, g_cfg.opacity, g_cfg.invert);

    BITMAPV5HEADER bi = {0};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = g_overlay.width;
    bi.bV5Height = -g_overlay.height; // top-down DIB
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(NULL);
    g_bits = NULL;
    g_bitmap = CreateDIBSection(hdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS,
                                &g_bits, NULL, 0);
    if (!g_bitmap) {
        ReleaseDC(NULL, hdc);
        return 0;
    }
    memcpy(g_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
    ReleaseDC(NULL, hdc);
    return 1;
}

static void update_window(void) {
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    SelectObject(mem, g_bitmap);

    SIZE size = {g_overlay.width, g_overlay.height};
    POINT src = {0, 0};
    POINT dst = {0, 0};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_hwnd, screen, &dst, &size, mem, &src, 0, &bf, ULW_ALPHA);

    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAY:
        if (lParam == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuA(menu, MF_STRING | (g_cfg.autostart ? MF_CHECKED : 0), 1, "Start at login");
            AppendMenuA(menu, MF_STRING | (g_cfg.invert ? MF_CHECKED : 0), 3, "Invert colors");
            AppendMenuA(menu, MF_STRING, 2, "Quit");
            POINT p; GetCursorPos(&p);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN, p.x, p.y, 0, hwnd, NULL);
            DestroyMenu(menu);
        }
        break;
    case WM_HOTKEY:
        if (wParam == 1) {
            static int visible = 0;
            visible = !visible;
            ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            g_cfg.autostart = !g_cfg.autostart;
            set_autostart(g_cfg.autostart);
            save_config(g_cfg_path, &g_cfg);
        } else if (LOWORD(wParam) == 2) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        } else if (LOWORD(wParam) == 3) {
            g_cfg.invert = !g_cfg.invert;
            apply_opacity_inversion(&g_overlay, g_cfg.opacity, g_cfg.invert);
            memcpy(g_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
            update_window();
            save_config(g_cfg_path, &g_cfg);
        }
        break;
    case WM_DESTROY:
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        UnregisterHotKey(NULL, 1);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShow) {
    if (load_config(g_cfg_path, &g_cfg) != 0) {
        strcpy(g_cfg.overlay_path, "keymap.png");
        g_cfg.opacity = 1.0f;
        g_cfg.invert = 0;
        g_cfg.autostart = 0;
        strcpy(g_cfg.hotkey, "Ctrl+Alt+Shift+Slash");
        g_cfg.persistent = 0;
        save_config(g_cfg_path, &g_cfg);
    }
    set_autostart(g_cfg.autostart);

    if (!init_bitmap()) {
        return 0;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "kbd_layout_overlay";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName, "", WS_POPUP, 0, 0, g_overlay.width, g_overlay.height,
        NULL, NULL, hInst, NULL);

    update_window();
    ShowWindow(g_hwnd, SW_HIDE);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpyA(g_nid.szTip, "Keyboard Layout Overlay");
    Shell_NotifyIconA(NIM_ADD, &g_nid);

    RegisterHotKey(NULL, 1, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_OEM_2);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteObject(g_bitmap);
    free_overlay(&g_overlay);
    return 0;
}

