#include "monitor.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

static int g_monitor_count = 0;
static MonitorInfo g_monitors[16]; // Support up to 16 monitors

static BOOL CALLBACK monitor_enum_proc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    if (g_monitor_count >= 16) return FALSE;
    
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfo(hMonitor, &mi)) {
        MonitorInfo *info = &g_monitors[g_monitor_count];
        info->x = mi.rcMonitor.left;
        info->y = mi.rcMonitor.top;
        info->width = mi.rcMonitor.right - mi.rcMonitor.left;
        info->height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        info->is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
        g_monitor_count++;
    }
    return TRUE;
}

static void refresh_monitors(void) {
    g_monitor_count = 0;
    EnumDisplayMonitors(NULL, NULL, monitor_enum_proc, 0);
}

int get_monitor_count(void) {
    refresh_monitors();
    return g_monitor_count;
}

int get_monitor_info(int monitor_index, MonitorInfo *info) {
    refresh_monitors();
    if (monitor_index < 1 || monitor_index > g_monitor_count || !info) {
        return -1;
    }
    *info = g_monitors[monitor_index - 1];
    return 0;
}

int get_primary_monitor(MonitorInfo *info) {
    refresh_monitors();
    if (!info) return -1;
    
    for (int i = 0; i < g_monitor_count; i++) {
        if (g_monitors[i].is_primary) {
            *info = g_monitors[i];
            return 0;
        }
    }
    return -1;
}

int get_active_monitor(MonitorInfo *info) {
    if (!info) return -1;
    
    HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfo(hmon, &mi)) {
            info->x = mi.rcMonitor.left;
            info->y = mi.rcMonitor.top;
            info->width = mi.rcMonitor.right - mi.rcMonitor.left;
            info->height = mi.rcMonitor.bottom - mi.rcMonitor.top;
            info->is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
            return 0;
        }
    }
    
    return get_primary_monitor(info);
}

#elif __APPLE__
#include <ApplicationServices/ApplicationServices.h>

int get_monitor_count(void) {
    uint32_t count = 0;
    CGGetActiveDisplayList(0, NULL, &count);
    return (int)count;
}

int get_monitor_info(int monitor_index, MonitorInfo *info) {
    if (!info || monitor_index < 1) return -1;
    
    uint32_t count = 0;
    CGGetActiveDisplayList(0, NULL, &count);
    if (monitor_index > (int)count) return -1;
    
    CGDirectDisplayID *displays = malloc(count * sizeof(CGDirectDisplayID));
    if (!displays) return -1;
    
    CGGetActiveDisplayList(count, displays, &count);
    
    if (monitor_index <= (int)count) {
        CGDirectDisplayID display = displays[monitor_index - 1];
        CGRect bounds = CGDisplayBounds(display);
        
        info->x = (int)bounds.origin.x;
        info->y = (int)bounds.origin.y;
        info->width = (int)bounds.size.width;
        info->height = (int)bounds.size.height;
        info->is_primary = (CGDisplayIsMain(display)) ? 1 : 0;
        
        free(displays);
        return 0;
    }
    
    free(displays);
    return -1;
}

int get_primary_monitor(MonitorInfo *info) {
    if (!info) return -1;
    
    CGDirectDisplayID main_display = CGMainDisplayID();
    CGRect bounds = CGDisplayBounds(main_display);
    
    info->x = (int)bounds.origin.x;
    info->y = (int)bounds.origin.y;
    info->width = (int)bounds.size.width;
    info->height = (int)bounds.size.height;
    info->is_primary = 1;
    
    return 0;
}

int get_active_monitor(MonitorInfo *info) {
    if (!info) return -1;
    
    // Try to get the monitor containing the mouse cursor
    CGEventRef event = CGEventCreate(NULL);
    if (event) {
        CGPoint cursor = CGEventGetLocation(event);
        CFRelease(event);
        
        uint32_t count = 0;
        CGGetActiveDisplayList(0, NULL, &count);
        CGDirectDisplayID *displays = malloc(count * sizeof(CGDirectDisplayID));
        if (displays) {
            CGGetActiveDisplayList(count, displays, &count);
            
            for (uint32_t i = 0; i < count; i++) {
                CGRect bounds = CGDisplayBounds(displays[i]);
                if (CGRectContainsPoint(bounds, cursor)) {
                    info->x = (int)bounds.origin.x;
                    info->y = (int)bounds.origin.y;
                    info->width = (int)bounds.size.width;
                    info->height = (int)bounds.size.height;
                    info->is_primary = (CGDisplayIsMain(displays[i])) ? 1 : 0;
                    
                    free(displays);
                    return 0;
                }
            }
            free(displays);
        }
    }
    
    // Fallback to primary monitor
    return get_primary_monitor(info);
}

#endif