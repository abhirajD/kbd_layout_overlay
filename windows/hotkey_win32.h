#ifndef HOTKEY_WIN32_H
#define HOTKEY_WIN32_H

#include <windows.h>

typedef struct HotkeyHandler HotkeyHandler;

HotkeyHandler* create_hotkey_handler(const char* hotkey_str, void(*callback)(void));
void destroy_hotkey_handler(HotkeyHandler* handler);
BOOL is_valid_hotkey_string(const char* hotkey_str);

#endif /* HOTKEY_WIN32_H */
