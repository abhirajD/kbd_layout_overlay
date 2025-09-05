#ifndef HOTKEY_MANAGER_H
#define HOTKEY_MANAGER_H

#include <windows.h>
#include "../shared/config.h"

#ifdef __cplusplus
extern "C" {
#endif

int hotkey_manager_init(Config *config);
void hotkey_manager_cleanup(void);
int hotkey_manager_register_hotkey(void);
void hotkey_manager_unregister_hotkey(void);
int hotkey_manager_parse_hotkey(const char *hotkey_str, UINT *modifiers, UINT *vk);
void hotkey_manager_set_toggle_callback(void (*callback)(void));
int hotkey_manager_is_valid_hotkey(const char *hotkey_str);

#ifdef __cplusplus
}
#endif

#endif // HOTKEY_MANAGER_H
