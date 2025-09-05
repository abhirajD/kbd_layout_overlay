#include "window_win.h"
#include <windows.h>
#include <stdlib.h>

struct OverlayWindow {
    HWND window;
    HDC screen_dc;
    HDC mem_dc;
    HBITMAP bitmap;
    void *bitmap_bits;
    int width;
    int height;
};

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        /* Route all mouse events to underlying windows */
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        /* Prevent the overlay from ever taking focus */
        return MA_NOACTIVATE;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

OverlayWindow* create_overlay_window(int width, int height) {
    OverlayWindow *overlay = calloc(1, sizeof(OverlayWindow));
    if (!overlay) return NULL;

    overlay->width = width;
    overlay->height = height;

    // Get screen DC for layered window
    overlay->screen_dc = GetDC(NULL);
    if (!overlay->screen_dc) {
        free(overlay);
        return NULL;
    }

    // Create compatible DC
    overlay->mem_dc = CreateCompatibleDC(overlay->screen_dc);
    if (!overlay->mem_dc) {
        ReleaseDC(NULL, overlay->screen_dc);
        free(overlay);
        return NULL;
    }

    // Create bitmap for the overlay content
    BITMAPV5HEADER bi = {0};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = width;
    bi.bV5Height = -height; /* Top-down DIB */
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    overlay->bitmap = CreateDIBSection(overlay->screen_dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS,
                                      &overlay->bitmap_bits, NULL, 0);
    if (!overlay->bitmap) {
        DeleteDC(overlay->mem_dc);
        ReleaseDC(NULL, overlay->screen_dc);
        free(overlay);
        return NULL;
    }

    // Register window class
    static int class_registered = 0;
    if (!class_registered) {
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "KbdLayoutOverlayWindow";
        RegisterClassA(&wc);
        class_registered = 1;
    }

    // Create layered window
    overlay->window = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "KbdLayoutOverlayWindow", "",
        WS_POPUP,
        0, 0, width, height,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    if (!overlay->window) {
        DeleteObject(overlay->bitmap);
        DeleteDC(overlay->mem_dc);
        ReleaseDC(NULL, overlay->screen_dc);
        free(overlay);
        return NULL;
    }

    return overlay;
}

void destroy_overlay_window(OverlayWindow* window) {
    if (!window) return;

    if (window->window) DestroyWindow(window->window);
    if (window->bitmap) DeleteObject(window->bitmap);
    if (window->mem_dc) DeleteDC(window->mem_dc);
    if (window->screen_dc) ReleaseDC(NULL, window->screen_dc);

    free(window);
}

void show_overlay_window(OverlayWindow* window) {
    if (!window || !window->window) return;

    ShowWindow(window->window, SW_SHOWNOACTIVATE);
}

void hide_overlay_window(OverlayWindow* window) {
    if (!window || !window->window) return;

    ShowWindow(window->window, SW_HIDE);
}

void set_overlay_position(OverlayWindow* window, int x, int y) {
    if (!window || !window->window) return;

    SetWindowPos(window->window, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void set_overlay_opacity(OverlayWindow* window, float opacity) {
    if (!window || !window->window) return;

    // Update layered window with new opacity
    SelectObject(window->mem_dc, window->bitmap);

    SIZE size = {window->width, window->height};
    POINT src = {0, 0};
    POINT dst = {0, 0}; // Will be set by position

    BLENDFUNCTION bf = {AC_SRC_OVER, 0, (BYTE)(opacity * 255), AC_SRC_ALPHA};

    // Get current position
    RECT rect;
    GetWindowRect(window->window, &rect);
    dst.x = rect.left;
    dst.y = rect.top;

    UpdateLayeredWindow(window->window, window->screen_dc, &dst, &size,
                       window->mem_dc, &src, 0, &bf, ULW_ALPHA);
}

void set_overlay_click_through(OverlayWindow* window, int enabled) {
    if (!window || !window->window) return;

    LONG ex_style = GetWindowLong(window->window, GWL_EXSTYLE);
    if (enabled) {
        ex_style |= WS_EX_TRANSPARENT;
    } else {
        ex_style &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLong(window->window, GWL_EXSTYLE, ex_style);
}

void set_overlay_always_on_top(OverlayWindow* window, int enabled) {
    if (!window || !window->window) return;

    if (enabled) {
        SetWindowPos(window->window, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        SetWindowPos(window->window, HWND_NOTOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void update_overlay_content(OverlayWindow* window, const unsigned char* rgba_data,
                           int width, int height) {
    if (!window || !rgba_data || !window->bitmap_bits) return;

    // Copy RGBA data to bitmap
    memcpy(window->bitmap_bits, rgba_data, (size_t)width * height * 4);

    // Update window content
    SelectObject(window->mem_dc, window->bitmap);

    SIZE size = {width, height};
    POINT src = {0, 0};
    POINT dst = {0, 0}; // Will be set by position

    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA}; // Default full opacity

    // Get current position
    RECT rect;
    GetWindowRect(window->window, &rect);
    dst.x = rect.left;
    dst.y = rect.top;

    UpdateLayeredWindow(window->window, window->screen_dc, &dst, &size,
                       window->mem_dc, &src, 0, &bf, ULW_ALPHA);
}
