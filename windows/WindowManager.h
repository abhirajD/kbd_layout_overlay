#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <windows.h>

struct Config;
struct Overlay;

#ifdef __cplusplus
extern "C" {
#endif

int window_manager_init(struct Config *config, struct Overlay *overlay);
void window_manager_cleanup(void);
HWND window_manager_get_window(void);
int window_manager_create_bitmap(void);
void window_manager_show_overlay(void);
void window_manager_hide_overlay(void);
void window_manager_toggle_overlay(void);
int window_manager_is_visible(void);
void window_manager_update_bitmap(void);
int window_manager_get_monitor_count(void);
RECT window_manager_get_monitor_rect(int monitor_index);

#ifdef __cplusplus
}
#endif

#endif // WINDOW_MANAGER_H
