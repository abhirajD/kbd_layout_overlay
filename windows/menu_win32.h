#ifndef MENU_WIN32_H
#define MENU_WIN32_H

#include <windows.h>

typedef struct TrayMenu TrayMenu;

TrayMenu* create_tray_menu(HWND hwnd, HINSTANCE hInstance, Config config,
                          void (*toggle_callback)(void), void (*quit_callback)(void));
void destroy_tray_menu(TrayMenu* menu);
void update_tray_menu(TrayMenu* menu, Config new_config);
void show_tray_menu(TrayMenu* menu, HWND hwnd);
void set_tray_icon_tooltip(TrayMenu* menu, const char* tooltip);

#endif /* MENU_WIN32_H */
