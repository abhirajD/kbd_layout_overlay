#include <windows.h>
#include "WindowManager.h"
#include "../shared/overlay.h"
#include "../shared/config.h"
#include "../shared/log.h"

static Config *g_config = NULL;
static Overlay *g_overlay = NULL;
static HWND g_window = NULL;
static HBITMAP g_bitmap = NULL;
static HDC g_screen_dc = NULL;
static HDC g_mem_dc = NULL;
static void *g_bitmap_bits = NULL;
static int g_visible = 0;

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

int window_manager_init(Config *config, Overlay *overlay) {
    g_config = config;
    g_overlay = overlay;

    /* Create overlay window on selected monitor */
    RECT mon = get_monitor_rect(g_config->monitor_index);
    g_window = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               "KbdLayoutOverlay", "", WS_POPUP,
                               mon.left, mon.top,
                               mon.right - mon.left,
                               mon.bottom - mon.top,
                               NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!g_window) {
        logger_log("Failed to create overlay window");
        return 0;
    }

    return 1;
}

void window_manager_cleanup(void) {
    if (g_bitmap) DeleteObject(g_bitmap);
    if (g_mem_dc) DeleteDC(g_mem_dc);
    if (g_screen_dc) ReleaseDC(NULL, g_screen_dc);
    g_window = NULL;
}

HWND window_manager_get_window(void) {
    return g_window;
}

int window_manager_create_bitmap(void) {
    BITMAPV5HEADER bi = {0};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = g_overlay->width;
    bi.bV5Height = -g_overlay->height; /* Top-down DIB */
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

    memcpy(g_bitmap_bits, g_overlay->data, (size_t)g_overlay->width * g_overlay->height * 4);
    return 1;
}

void window_manager_show_overlay(void) {
    if (!g_window) return;

    SelectObject(g_mem_dc, g_bitmap);

    /* Use selected monitor bounds */
    RECT mon = get_monitor_rect(g_config->monitor_index);
    int screen_w = mon.right - mon.left;
    int screen_h = mon.bottom - mon.top;

    /* Center overlay on chosen monitor */
    int x = mon.left + (screen_w - g_overlay->width) / 2 + g_config->position_x;
    int y = mon.top + screen_h - g_overlay->height - g_config->position_y;

    SetWindowPos(g_window, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);

    SIZE size = {g_overlay->width, g_overlay->height};
    POINT src = {0, 0};
    POINT dst = {x, y};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    UpdateLayeredWindow(g_window, g_screen_dc, &dst, &size, g_mem_dc, &src, 0, &bf, ULW_ALPHA);
    ShowWindow(g_window, SW_SHOWNOACTIVATE);
    g_visible = 1;

    /* Auto-hide timer when enabled */
    if (g_config->auto_hide > 0.0f) {
        SetTimer(g_window, 1, (UINT)(g_config->auto_hide * 1000), NULL);
    }

    logger_log("Overlay shown at (%d, %d) size=(%dx%d)", x, y, g_overlay->width, g_overlay->height);
}

void window_manager_hide_overlay(void) {
    if (g_window && g_visible) {
        /* Kill auto-hide timer if active */
        KillTimer(g_window, 1);
        ShowWindow(g_window, SW_HIDE);
        g_visible = 0;
        logger_log("Overlay hidden");
    }
}

void window_manager_toggle_overlay(void) {
    if (g_visible) {
        window_manager_hide_overlay();
    } else {
        window_manager_show_overlay();
    }
}

int window_manager_is_visible(void) {
    return g_visible;
}

void window_manager_update_bitmap(void) {
    if (g_bitmap_bits && g_overlay) {
        memcpy(g_bitmap_bits, g_overlay->data, (size_t)g_overlay->width * g_overlay->height * 4);
    }
}

int window_manager_get_monitor_count(void) {
    return get_monitor_count();
}

RECT window_manager_get_monitor_rect(int monitor_index) {
    return get_monitor_rect(monitor_index);
}
