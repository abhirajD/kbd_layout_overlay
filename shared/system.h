#ifndef SYSTEM_H
#define SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Cross-platform system integration abstraction */

typedef struct SystemTray SystemTray;
typedef struct HotkeyHandler HotkeyHandler;

/* Tray menu creation and management */
SystemTray* create_system_tray(void);
void destroy_system_tray(SystemTray* tray);
void update_tray_menu(SystemTray* tray);

/* Hotkey registration and management */
HotkeyHandler* register_hotkey(const char* hotkey_str, void(*callback)(void));
void unregister_hotkey(HotkeyHandler* handler);

/* System notifications */
void show_notification(const char* title, const char* message);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_H */
