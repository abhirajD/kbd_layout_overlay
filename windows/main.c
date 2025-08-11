#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../shared/overlay.h"

static Overlay g_overlay;
static HBITMAP g_bitmap;
static HWND g_hwnd;

static void register_autostart(void) {
    HKEY key;
    if (RegCreateKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0,
            KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        RegSetValueExA(key, "kbd_layout_overlay", 0, REG_SZ,
            (const BYTE *)path, (DWORD)(strlen(path) + 1));
        RegCloseKey(key);
    }
}

static int init_bitmap(void) {
    if (load_overlay_image("keymap.png", &g_overlay) != 0) {
        MessageBoxA(NULL, "Failed to load keymap.png", "Error", MB_OK);
        return 0;
    }

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
    void *bits = NULL;
    g_bitmap = CreateDIBSection(hdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS,
                                &bits, NULL, 0);
    if (!g_bitmap) {
        ReleaseDC(NULL, hdc);
        return 0;
    }
    memcpy(bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
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
    case WM_HOTKEY:
        if (wParam == 1) {
            static int visible = 0;
            visible = !visible;
            ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
        }
        break;
    case WM_DESTROY:
        UnregisterHotKey(NULL, 1);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShow) {
    register_autostart();

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

