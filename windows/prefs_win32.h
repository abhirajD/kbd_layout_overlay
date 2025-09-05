#ifndef PREFS_WIN32_H
#define PREFS_WIN32_H

#include <windows.h>
#include "../shared/config.h"

typedef struct PreferencesDialog PreferencesDialog;

PreferencesDialog* create_preferences_dialog(HINSTANCE hInstance, Config* config, void (*apply_callback)(Config*));
void destroy_preferences_dialog(PreferencesDialog* prefs);
BOOL show_preferences_dialog(PreferencesDialog* prefs, HINSTANCE hInstance, HWND parent_hwnd);
void update_preferences_config(PreferencesDialog* prefs, Config* new_config);

#endif /* PREFS_WIN32_H */
