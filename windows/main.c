#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "../shared/config.h"
#include "../shared/overlay.h"
#include "../shared/hotkey.h"
#include "../shared/monitor.h"
#include "../shared/error.h"
#include "../shared/app_context.h"
#include "resource.h"

/* Single global application context replaces all previous globals */
static klo_app_context_t g_app_ctx;

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

static void parse_hotkey_win(const char *hotkey, klo_hotkey_context_t *hk_ctx) {
    hotkey_t hk;
    parse_hotkey(hotkey, &hk);

    hk_ctx->modifiers = 0;
    if (hk.mods & HOTKEY_MOD_CTRL) hk_ctx->modifiers |= MOD_CONTROL;
    if (hk.mods & HOTKEY_MOD_ALT) hk_ctx->modifiers |= MOD_ALT;
    if (hk.mods & HOTKEY_MOD_SHIFT) hk_ctx->modifiers |= MOD_SHIFT;
    if (hk.mods & HOTKEY_MOD_SUPER) hk_ctx->modifiers |= MOD_WIN;

    if (hk.key >= 'A' && hk.key <= 'Z') hk_ctx->virtual_key = hk.key;
    else if (hk.key >= '0' && hk.key <= '9') hk_ctx->virtual_key = hk.key;
    else if (hk.key == '/') hk_ctx->virtual_key = VK_OEM_2;
    else hk_ctx->virtual_key = VK_OEM_2;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && !g_app_ctx.config.persistent && g_app_ctx.hotkey.is_active) {
        if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lParam;
            int hide = 0;
            if (k->vkCode == g_app_ctx.hotkey.virtual_key) hide = 1;
            if (!hide && (g_app_ctx.hotkey.modifiers & MOD_CONTROL) &&
                (k->vkCode == VK_LCONTROL || k->vkCode == VK_RCONTROL)) hide = 1;
            if (!hide && (g_app_ctx.hotkey.modifiers & MOD_ALT) &&
                (k->vkCode == VK_LMENU || k->vkCode == VK_RMENU)) hide = 1;
            if (!hide && (g_app_ctx.hotkey.modifiers & MOD_SHIFT) &&
                (k->vkCode == VK_LSHIFT || k->vkCode == VK_RSHIFT)) hide = 1;
            if (!hide && (g_app_ctx.hotkey.modifiers & MOD_WIN) &&
                (k->vkCode == VK_LWIN || k->vkCode == VK_RWIN)) hide = 1;
            if (hide) {
                ShowWindow((HWND)g_app_ctx.ui.window, SW_HIDE);
                g_app_ctx.hotkey.is_active = 0;
                g_app_ctx.ui.is_visible = 0;
            }
        }
    }
    return CallNextHookEx((HHOOK)g_app_ctx.hotkey.hook, nCode, wParam, lParam);
}

static int get_target_monitor_info(klo_app_context_t *ctx, MonitorInfo *info) {
    if (ctx->config.monitor == 0) {
        // Auto: use active monitor
        return get_active_monitor(info);
    } else if (ctx->config.monitor == 1) {
        // Primary monitor
        return get_primary_monitor(info);
    } else {
        // Specific monitor (2, 3, etc.)
        if (get_monitor_info(ctx->config.monitor, info) == 0) {
            return 0;
        }
    }
    
    // Fallback to primary monitor
    return get_primary_monitor(info);
}

static int get_target_monitor_size(klo_app_context_t *ctx, int *width, int *height) {
    MonitorInfo info;
    if (get_target_monitor_info(ctx, &info) == 0) {
        *width = info.width;
        *height = info.height;
        return 0;
    }
    
    // Last resort fallback
    *width = GetSystemMetrics(SM_CXSCREEN);
    *height = GetSystemMetrics(SM_CYSCREEN);
    return 0;
}

static int init_bitmap(klo_app_context_t *ctx) {
    const char *path = (ctx->config.overlay_path && ctx->config.overlay_path[0]) ? ctx->config.overlay_path : "keymap.png";
    int screen_w, screen_h;
    get_target_monitor_size(ctx, &screen_w, &screen_h);
    int r = load_overlay_image(path, screen_w, screen_h, &ctx->overlay);
    if (r != OVERLAY_OK) {
        const char *fmt;
        if (r == OVERLAY_ERR_NOT_FOUND)
            fmt = "Overlay image not found: %s";
        else if (r == OVERLAY_ERR_DECODE)
            fmt = "Failed to decode overlay image: %s";
        else
            fmt = "Failed to load overlay image: %s";
        char buf[256];
        snprintf(buf, sizeof(buf), fmt, path);
        MessageBoxA(NULL, buf, "Error", MB_OK);
        // Try embedded resource as fallback regardless of path configuration
        HRSRC res = FindResourceA(NULL, MAKEINTRESOURCEA(IDR_KEYMAP), RT_RCDATA);
        if (res) {
            HGLOBAL data = LoadResource(NULL, res);
            DWORD size = SizeofResource(NULL, res);
            void *ptr = LockResource(data);
            if (ptr && size) {
                r = load_overlay_image_mem(ptr, (int)size, screen_w, screen_h, &ctx->overlay);
            }
        }
        if (r != OVERLAY_OK) {
            MessageBoxA(NULL, "Failed to load overlay image", "Error", MB_OK);
            return 0;
        }
    }
    // Initialize cache asynchronously for faster startup
    if (init_overlay_cache_async(&ctx->overlay_cache, &ctx->overlay) != OVERLAY_OK) {
        // Fallback to synchronous cache if async fails
        if (init_overlay_cache(&ctx->overlay_cache, &ctx->overlay) != OVERLAY_OK) {
            MessageBoxA(NULL, "Failed to initialize image cache", "Error", MB_OK);
            return 0;
        }
    }

    // Get initial cached variation (may not be ready yet if async)
    const Overlay *cached = get_cached_variation(&ctx->overlay_cache, ctx->config.opacity, ctx->config.invert);
    if (!cached) {
        // Cache not ready yet - use original image temporarily
        cached = &ctx->overlay;
    }

    BITMAPV5HEADER bi = {0};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = cached->width;
    bi.bV5Height = -cached->height; // top-down DIB
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(NULL);
    ctx->graphics.bitmap_bits = NULL;
    ctx->graphics.bitmap = (klo_bitmap_t)CreateDIBSection(hdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS,
                                &ctx->graphics.bitmap_bits, NULL, 0);
    if (!ctx->graphics.bitmap) {
        ReleaseDC(NULL, hdc);
        return 0;
    }
    memcpy(ctx->graphics.bitmap_bits, cached->data, (size_t)cached->width * cached->height * 4);
    ReleaseDC(NULL, hdc);
    return 1;
}

static int init_graphics_resources(klo_app_context_t *ctx) {
    ctx->graphics.screen_dc = (klo_device_context_t)GetDC(NULL);
    if (!ctx->graphics.screen_dc) return 0;
    
    ctx->graphics.mem_dc = (klo_device_context_t)CreateCompatibleDC((HDC)ctx->graphics.screen_dc);
    if (!ctx->graphics.mem_dc) {
        ReleaseDC(NULL, (HDC)ctx->graphics.screen_dc);
        ctx->graphics.screen_dc = NULL;
        return 0;
    }
    
    return 1;
}

static void cleanup_graphics_resources(klo_app_context_t *ctx) {
    if (ctx->graphics.mem_dc) {
        DeleteDC((HDC)ctx->graphics.mem_dc);
        ctx->graphics.mem_dc = NULL;
    }
    if (ctx->graphics.screen_dc) {
        ReleaseDC(NULL, (HDC)ctx->graphics.screen_dc);
        ctx->graphics.screen_dc = NULL;
    }
}

static void update_bitmap_from_cache(klo_app_context_t *ctx) {
    const Overlay *cached = get_cached_variation(&ctx->overlay_cache, ctx->config.opacity, ctx->config.invert);
    if (cached && ctx->graphics.bitmap_bits) {
        memcpy(ctx->graphics.bitmap_bits, cached->data, (size_t)cached->width * cached->height * 4);
    }
}

static void position_window_on_monitor(klo_app_context_t *ctx) {
    MonitorInfo info;
    if (get_target_monitor_info(ctx, &info) == 0) {
        // Position at bottom center of the target monitor
        int x = info.x + (info.width - ctx->overlay_cache.base_width) / 2;
        int y = info.y + info.height - ctx->overlay_cache.base_height - 50; // 50px from bottom
        SetWindowPos((HWND)ctx->ui.window, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

static void update_window(klo_app_context_t *ctx) {
    if (!ctx->graphics.screen_dc || !ctx->graphics.mem_dc || !ctx->graphics.bitmap) return;
    
    SelectObject((HDC)ctx->graphics.mem_dc, (HBITMAP)ctx->graphics.bitmap);

    // Use cache dimensions (all variations should have same dimensions)
    SIZE size = {ctx->overlay_cache.base_width, ctx->overlay_cache.base_height};
    POINT src = {0, 0};
    POINT dst = {0, 0};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow((HWND)ctx->ui.window, (HDC)ctx->graphics.screen_dc, &dst, &size, (HDC)ctx->graphics.mem_dc, &src, 0, &bf, ULW_ALPHA)) {
        DWORD err = GetLastError();
        char buf[128];
        snprintf(buf, sizeof(buf), "UpdateLayeredWindow failed: %lu\n", (unsigned long)err);
        OutputDebugStringA(buf);
    }
}

static void show_tray_menu(HWND hwnd) {
    klo_app_context_t *ctx = (klo_app_context_t*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!ctx) ctx = &g_app_ctx;

    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING | (ctx->config.autostart ? MF_CHECKED : 0), 1, "Start at login");
    AppendMenuA(menu, MF_STRING | (ctx->config.persistent ? MF_CHECKED : 0), 5, "Persistent mode");
    AppendMenuA(menu, MF_STRING | (ctx->config.invert ? MF_CHECKED : 0), 3, "Invert colors");
    AppendMenuA(menu, MF_STRING, 4, "Cycle opacity");
    // Show current monitor status
    char monitor_text[64];
    if (ctx->config.monitor == 0) {
        strncpy(monitor_text, "Cycle monitor (Auto)", sizeof(monitor_text) - 1);
        monitor_text[sizeof(monitor_text) - 1] = '\0';
    } else {
        snprintf(monitor_text, sizeof(monitor_text), "Cycle monitor (%d)", ctx->config.monitor);
    }
    AppendMenuA(menu, MF_STRING, 6, monitor_text);
    AppendMenuA(menu, MF_STRING, 2, "Quit");
    POINT p; GetCursorPos(&p);
    SetForegroundWindow((HWND)ctx->ui.window ? (HWND)ctx->ui.window : hwnd);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN, p.x, p.y, 0, (HWND)(ctx->ui.window ? ctx->ui.window : hwnd), NULL);
    DestroyMenu(menu);
}

static void on_hotkey(HWND hwnd) {
    klo_app_context_t *ctx = (klo_app_context_t*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!ctx) ctx = &g_app_ctx;

    if (ctx->config.persistent) {
        ctx->ui.is_visible = !ctx->ui.is_visible;
        if (ctx->ui.is_visible) {
            position_window_on_monitor(ctx);
            ShowWindow((HWND)ctx->ui.window, SW_SHOW);
            update_window(ctx);
        } else {
            ShowWindow((HWND)ctx->ui.window, SW_HIDE);
        }
    } else {
        ctx->hotkey.is_active = 1;
        ctx->ui.is_visible = 1;
        position_window_on_monitor(ctx);
        ShowWindow((HWND)ctx->ui.window, SW_SHOW);
        update_window(ctx);
    }
}

static void on_command(HWND hwnd, WPARAM wParam) {
    klo_app_context_t *ctx = (klo_app_context_t*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!ctx) ctx = &g_app_ctx;

    switch (LOWORD(wParam)) {
    case 1:
        ctx->config.autostart = !ctx->config.autostart;
        set_autostart(ctx->config.autostart);
        klo_context_save_config(ctx);
        break;
    case 2:
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
    case 3:
        ctx->config.invert = !ctx->config.invert;
        update_bitmap_from_cache(ctx);
        update_window(ctx);
        klo_context_save_config(ctx);
        break;
    case 4: {
        float levels[] = {0.25f, 0.5f, 0.75f, 1.0f};
        int count = sizeof(levels) / sizeof(levels[0]);
        int next = 0;
        for (int i = 0; i < count; i++) {
            if (ctx->config.opacity <= levels[i] + 0.001f) {
                next = (i + 1) % count;
                break;
            }
        }
        ctx->config.opacity = levels[next];
        update_bitmap_from_cache(ctx);
        update_window(ctx);
        klo_context_save_config(ctx);
        break;
    }
    case 5:
        ctx->config.persistent = !ctx->config.persistent;
        // Dynamically manage keyboard hook based on persistent mode
        if (ctx->config.persistent && ctx->hotkey.hook) {
            UnhookWindowsHookEx((HHOOK)ctx->hotkey.hook);
            ctx->hotkey.hook = NULL;
        } else if (!ctx->config.persistent && !ctx->hotkey.hook) {
            ctx->hotkey.hook = (klo_hook_t)SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
        }
        // Hide overlay if switching to persistent mode while visible
        if (ctx->config.persistent && ctx->ui.is_visible) {
            ShowWindow((HWND)ctx->ui.window, SW_HIDE);
            ctx->ui.is_visible = 0;
            ctx->hotkey.is_active = 0;
        }
        klo_context_save_config(ctx);
        break;
    case 6: {
        // Cycle through monitors: 0=auto, 1=primary, 2=secondary, etc.
        int monitor_count = get_monitor_count();
        ctx->config.monitor = (ctx->config.monitor + 1) % (monitor_count + 1); // +1 for auto mode
        klo_context_save_config(ctx);
        break;
    }
    }
    }
}

static void on_destroy(void) {
    Shell_NotifyIconA(NIM_DELETE, &g_app_ctx.ui.tray_icon);
    if (g_app_ctx.hotkey.is_registered) {
        UnregisterHotKey(NULL, 1);
        g_app_ctx.hotkey.is_registered = 0;
    }
    if (g_app_ctx.hotkey.hook) {
        UnhookWindowsHookEx((HHOOK)g_app_ctx.hotkey.hook);
        g_app_ctx.hotkey.hook = NULL;
    }
    cleanup_graphics_resources(&g_app_ctx);
    free_overlay_cache(&g_app_ctx.overlay_cache);
    free_config(&g_app_ctx.config);
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

/* Windows-specific error handler */
static void windows_error_handler(const klo_error_context_t *ctx) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s\n\nFile: %s:%d\nFunction: %s", 
             ctx->message, ctx->file, ctx->line, ctx->function);
    MessageBoxA(NULL, buf, "Keyboard Layout Overlay Error", MB_OK | MB_ICONERROR);
}

static void load_default_config(void) {
    /* Initialize application context with defaults and save */
    klo_error_t err = klo_context_init(&g_app_ctx, "config.cfg");
    if (err != KLO_OK) {
        MessageBoxA(NULL, klo_error_get_message(), "Configuration Error", MB_OK | MB_ICONERROR);
        exit(1);
    }
    klo_context_save_config(&g_app_ctx);
}

static void init_windows_config(void) {
    /* Initialize error handling first */
    klo_error_init(windows_error_handler);
    
    /* Initialize application context */
    klo_error_t err = klo_context_init(&g_app_ctx, "config.cfg");
    if (err != KLO_OK) {
        klo_log(KLO_LOG_FATAL, "Failed to initialize application context: %s", klo_error_get_message());
        exit(1);
    }
    
    /* Try to load configuration from file */
    err = klo_context_load_config(&g_app_ctx);
    if (err != KLO_OK) {
        /* File doesn't exist or is corrupted, save defaults */
        klo_log(KLO_LOG_INFO, "Creating default configuration file");
        klo_context_save_config(&g_app_ctx);
    }
    
    /* Validate hotkey field */
    if (!g_app_ctx.config.hotkey || !g_app_ctx.config.hotkey[0]) {
        err = set_config_string(&g_app_ctx.config.hotkey, "Ctrl+Alt+Shift+Slash");
        if (err != KLO_OK) {
            klo_log(KLO_LOG_FATAL, "Failed to set default hotkey: %s", klo_error_get_message());
            exit(1);
        }
    }
    
    set_autostart(g_app_ctx.config.autostart);
}

static int init_overlay_window(HINSTANCE hInst) {
    if (!init_bitmap(&g_app_ctx)) {
        return 0;
    }

    if (!init_graphics_resources(&g_app_ctx)) {
        return 0;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "kbd_layout_overlay";
    RegisterClassA(&wc);

    g_app_ctx.ui.window = (klo_window_handle_t)CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName, "", WS_POPUP, 0, 0, g_app_ctx.overlay.width, g_app_ctx.overlay.height,
        NULL, NULL, hInst, NULL);

    /* Store pointer to context on the window for use in WndProc if needed */
    SetWindowLongPtr((HWND)g_app_ctx.ui.window, GWLP_USERDATA, (LONG_PTR)&g_app_ctx);

    ShowWindow((HWND)g_app_ctx.ui.window, SW_HIDE);

    {
        NOTIFYICONDATAA nid = {0};
        nid.cbSize = sizeof(nid);
        nid.hWnd = (HWND)g_app_ctx.ui.window;
        nid.uID = 1;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_TRAY;
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        strncpy(nid.szTip, "Keyboard Layout Overlay", sizeof(nid.szTip) - 1);
        nid.szTip[sizeof(nid.szTip) - 1] = '\0';
        Shell_NotifyIconA(NIM_ADD, &nid);
        /* store a copy in the UI context for later removal */
        memcpy(&g_app_ctx.ui.tray_icon, &nid, sizeof(g_app_ctx.ui.tray_icon));
    }

    return 1;
}

static void register_hotkey(void) {
    /* Populate platform hotkey context from config */
    parse_hotkey_win(g_app_ctx.config.hotkey, &g_app_ctx.hotkey);
    RegisterHotKey(NULL, 1, g_app_ctx.hotkey.modifiers | MOD_NOREPEAT, g_app_ctx.hotkey.virtual_key);
    g_app_ctx.hotkey.is_registered = 1;
    if (!g_app_ctx.config.persistent) {
        g_app_ctx.hotkey.hook = (klo_hook_t)SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShow) {
    init_windows_config();
    if (!init_overlay_window(hInst)) {
        return 0;
    }
    register_hotkey();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_app_ctx.graphics.bitmap) {
        DeleteObject((HGDIOBJ)g_app_ctx.graphics.bitmap);
        g_app_ctx.graphics.bitmap = NULL;
    }
    free_overlay(&g_app_ctx.overlay);
    return 0;
}
