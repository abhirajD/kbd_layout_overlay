#ifndef MENU_CONTROLLER_H
#define MENU_CONTROLLER_H

#include <windows.h>
#include "../shared/config.h"

#ifdef __cplusplus
extern "C" {
#endif

int menu_controller_init(Config *config, HINSTANCE hInstance, HWND hwnd);
void menu_controller_cleanup(void);
void menu_controller_set_callbacks(
    void (*show_callback)(void),
    void (*hide_callback)(void),
    void (*toggle_callback)(void),
    void (*config_changed_callback)(void)
);
void menu_controller_handle_command(int command);
void menu_controller_show_menu(void);
void menu_controller_update_menu(void);

#ifdef __cplusplus
}
#endif

#endif // MENU_CONTROLLER_H
