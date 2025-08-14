#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "../shared/config.h"
#include "../shared/overlay.h"
#include "../shared/hotkey.h"
#include "resource.h"

static Overlay g_overlay;
static HBITMAP g_bitmap;
static void *g_bits;
static HWND g_hwnd;
static Config g_cfg;
static NOTIFYICONDATAA g_nid;
static char g_cfg_path[MAX_PATH] = "config.cfg";
#define WM_TRAY (WM_APP + 1)

static UINT g_hotkey_vk;
static UINT g_hotkey_mods;
static int g_hotkey_active;
static int g_visible;
static HHOOK g_hook;

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

static void parse_hotkey_win(const char *hotkey, UINT *mods, UINT *vk) {
    hotkey_t hk;
    parse_hotkey(hotkey, &hk);

    *mods = 0;
    if (hk.mods & HOTKEY_MOD_CTRL) *mods |= MOD_CONTROL;
    if (hk.mods & HOTKEY_MOD_ALT) *mods |= MOD_ALT;
    if (hk.mods & HOTKEY_MOD_SHIFT) *mods |= MOD_SHIFT;
    if (hk.mods & HOTKEY_MOD_SUPER) *mods |= MOD_WIN;

    if (hk.key >= 'A' && hk.key <= 'Z') *vk = hk.key;
    else if (hk.key >= '0' && hk.key <= '9') *vk = hk.key;
    else if (hk.key == '/') *vk = VK_OEM_2;
    else *vk = VK_OEM_2;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && !g_cfg.persistent && g_hotkey_active) {
        if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lParam;
            int hide = 0;
            if (k->vkCode == g_hotkey_vk) hide = 1;
            if (!hide && (g_hotkey_mods & MOD_CONTROL) &&
                (k->vkCode == VK_LCONTROL || k->vkCode == VK_RCONTROL)) hide = 1;
            if (!hide && (g_hotkey_mods & MOD_ALT) &&
                (k->vkCode == VK_LMENU || k->vkCode == VK_RMENU)) hide = 1;
            if (!hide && (g_hotkey_mods & MOD_SHIFT) &&
                (k->vkCode == VK_LSHIFT || k->vkCode == VK_RSHIFT)) hide = 1;
            if (!hide && (g_hotkey_mods & MOD_WIN) &&
                (k->vkCode == VK_LWIN || k->vkCode == VK_RWIN)) hide = 1;
            if (hide) {
                ShowWindow(g_hwnd, SW_HIDE);
                g_hotkey_active = 0;
                g_visible = 0;
            }
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static int init_bitmap(void) {
    const char *path = g_cfg.overlay_path[0] ? g_cfg.overlay_path : "keymap.png";
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int r = load_overlay_image(path, screen_w, screen_h, &g_overlay);
    if (r != 0 && !g_cfg.overlay_path[0]) {
        HRSRC res = FindResourceA(NULL, MAKEINTRESOURCEA(IDR_KEYMAP), RT_RCDATA);
        if (res) {
            HGLOBAL data = LoadResource(NULL, res);
            DWORD size = SizeofResource(NULL, res);
            void *ptr = LockResource(data);
            if (ptr && size) {
                r = load_overlay_image_mem(ptr, (int)size, screen_w, screen_h, &g_overlay);
            }
        }
    }
    if (r != 0) {
        MessageBoxA(NULL, "Failed to load overlay image", "Error", MB_OK);
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
    if (!UpdateLayeredWindow(g_hwnd, screen, &dst, &size, mem, &src, 0, &bf, ULW_ALPHA)) {
        DWORD err = GetLastError();
        char buf[128];
        snprintf(buf, sizeof(buf), "UpdateLayeredWindow failed: %lu\n", (unsigned long)err);
        OutputDebugStringA(buf);
    }

    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static void show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING | (g_cfg.autostart ? MF_CHECKED : 0), 1, "Start at login");
    AppendMenuA(menu, MF_STRING | (g_cfg.invert ? MF_CHECKED : 0), 3, "Invert colors");
    AppendMenuA(menu, MF_STRING, 4, "Cycle opacity");
    AppendMenuA(menu, MF_STRING, 2, "Quit");
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN, p.x, p.y, 0, hwnd, NULL);
    DestroyMenu(menu);
}

static void on_hotkey(HWND hwnd) {
    if (g_cfg.persistent) {
        g_visible = !g_visible;
        ShowWindow(hwnd, g_visible ? SW_SHOW : SW_HIDE);
        if (g_visible) update_window();
    } else {
        g_hotkey_active = 1;
        g_visible = 1;
        ShowWindow(hwnd, SW_SHOW);
        update_window();
    }
}

static void on_command(HWND hwnd, WPARAM wParam) {
    switch (LOWORD(wParam)) {
    case 1:
        g_cfg.autostart = !g_cfg.autostart;
        set_autostart(g_cfg.autostart);
        save_config(g_cfg_path, &g_cfg);
        break;
    case 2:
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
    case 3:
        g_cfg.invert = !g_cfg.invert;
        apply_opacity_inversion(&g_overlay, g_cfg.opacity, g_cfg.invert);
        memcpy(g_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
        update_window();
        save_config(g_cfg_path, &g_cfg);
        break;
    case 4: {
        float levels[] = {0.25f, 0.5f, 0.75f, 1.0f};
        int count = sizeof(levels) / sizeof(levels[0]);
        int next = 0;
        for (int i = 0; i < count; i++) {
            if (g_cfg.opacity <= levels[i] + 0.001f) {
                next = (i + 1) % count;
                break;
            }
        }
        g_cfg.opacity = levels[next];
        apply_opacity_inversion(&g_overlay, g_cfg.opacity, g_cfg.invert);
        memcpy(g_bits, g_overlay.data, (size_t)g_overlay.width * g_overlay.height * 4);
        update_window();
        save_config(g_cfg_path, &g_cfg);
        break;
    }
    }
}

static void on_destroy(void) {
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
    UnregisterHotKey(NULL, 1);
    if (g_hook) UnhookWindowsHookEx(g_hook);
    PostQuitMessage(0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAY:
        if (lParam == WM_RBUTTONUP) show_tray_menu(hwnd);
        break;
    case WM_HOTKEY:
        if (wParam == 1) on_hotkey(hwnd);
        break;
    case WM_COMMAND:
        on_command(hwnd, wParam);
        break;
    case WM_DESTROY:
        on_destroy();
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

static void load_default_config(void) {
    strcpy(g_cfg.overlay_path, "keymap.png");
    g_cfg.opacity = 1.0f;
    g_cfg.invert = 0;
    g_cfg.autostart = 0;
    strcpy(g_cfg.hotkey, "Ctrl+Alt+Shift+Slash");
    g_cfg.persistent = 0;
    save_config(g_cfg_path, &g_cfg);
}

static void init_config(void) {
    if (load_config(g_cfg_path, &g_cfg) != 0) {
        load_default_config();
    }
    if (!g_cfg.hotkey[0]) {
        strcpy(g_cfg.hotkey, "Ctrl+Alt+Shift+Slash");
    }
    set_autostart(g_cfg.autostart);
}

static int init_overlay_window(HINSTANCE hInst) {
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

    ShowWindow(g_hwnd, SW_HIDE);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpyA(g_nid.szTip, "Keyboard Layout Overlay");
    Shell_NotifyIconA(NIM_ADD, &g_nid);

    return 1;
}

static void register_hotkey(void) {
    parse_hotkey_win(g_cfg.hotkey, &g_hotkey_mods, &g_hotkey_vk);
    RegisterHotKey(NULL, 1, g_hotkey_mods | MOD_NOREPEAT, g_hotkey_vk);
    if (!g_cfg.persistent) {
        g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShow) {
    init_config();
    if (!init_overlay_window(hInst)) {
        return 0;
    }
    register_hotkey();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteObject(g_bitmap);
    free_overlay(&g_overlay);
    return 0;
}

