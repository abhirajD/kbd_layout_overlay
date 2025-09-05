#include "prefs_win32.h"
#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "../shared/config.h"
#include "../shared/log.h"

#define IDC_SCALE_SLIDER 1001
#define IDC_SCALE_LABEL 1002
#define IDC_OPACITY_SLIDER 1003
#define IDC_OPACITY_LABEL 1004
#define IDC_AUTO_HIDE_COMBO 1005
#define IDC_POSITION_COMBO 1006
#define IDC_CLICK_THROUGH_CHECK 1007
#define IDC_ALWAYS_TOP_CHECK 1008
#define IDC_HOTKEY_EDIT 1009
#define IDC_USE_CUSTOM_SIZE_CHECK 1010
#define IDC_WIDTH_EDIT 1011
#define IDC_HEIGHT_EDIT 1012
#define IDC_ERROR_LABEL 1013
#define IDC_APPLY_BUTTON 1014
#define IDC_CLOSE_BUTTON 1015
#define IDC_RESTORE_BUTTON 1016

struct PreferencesDialog {
    Config config;
    Config original_config;
    HWND hwnd;
    void (*apply_callback)(Config);
};

static PreferencesDialog *g_current_prefs = NULL;

static INT_PTR CALLBACK PreferencesDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            g_current_prefs = (PreferencesDialog*)lParam;
            if (!g_current_prefs) return FALSE;

            g_current_prefs->hwnd = hwnd;

            // Initialize controls with current config values
            SendDlgItemMessage(hwnd, IDC_SCALE_SLIDER, TBM_SETRANGE, TRUE, MAKELONG(50, 150));
            SendDlgItemMessage(hwnd, IDC_SCALE_SLIDER, TBM_SETPOS, TRUE, (int)(g_current_prefs->config.scale * 100));

            SendDlgItemMessage(hwnd, IDC_OPACITY_SLIDER, TBM_SETRANGE, TRUE, MAKELONG(30, 100));
            SendDlgItemMessage(hwnd, IDC_OPACITY_SLIDER, TBM_SETPOS, TRUE, (int)(g_current_prefs->config.opacity * 100));

            // Auto-hide combo
            SendDlgItemMessage(hwnd, IDC_AUTO_HIDE_COMBO, CB_ADDSTRING, 0, (LPARAM)"Off (Persistent)");
            SendDlgItemMessage(hwnd, IDC_AUTO_HIDE_COMBO, CB_ADDSTRING, 0, (LPARAM)"0.8s");
            SendDlgItemMessage(hwnd, IDC_AUTO_HIDE_COMBO, CB_ADDSTRING, 0, (LPARAM)"2.0s");

            int auto_hide_idx = 0;
            if (g_current_prefs->config.auto_hide > 0.0f) {
                if (fabsf(g_current_prefs->config.auto_hide - 0.8f) < 0.001f) auto_hide_idx = 1;
                else if (fabsf(g_current_prefs->config.auto_hide - 2.0f) < 0.001f) auto_hide_idx = 2;
            }
            SendDlgItemMessage(hwnd, IDC_AUTO_HIDE_COMBO, CB_SETCURSEL, auto_hide_idx, 0);

            // Position combo
            SendDlgItemMessage(hwnd, IDC_POSITION_COMBO, CB_ADDSTRING, 0, (LPARAM)"Center");
            SendDlgItemMessage(hwnd, IDC_POSITION_COMBO, CB_ADDSTRING, 0, (LPARAM)"Top-Center");
            SendDlgItemMessage(hwnd, IDC_POSITION_COMBO, CB_ADDSTRING, 0, (LPARAM)"Bottom-Center");
            SendDlgItemMessage(hwnd, IDC_POSITION_COMBO, CB_ADDSTRING, 0, (LPARAM)"Custom");
            SendDlgItemMessage(hwnd, IDC_POSITION_COMBO, CB_SETCURSEL, g_current_prefs->config.position_mode, 0);

            // Checkboxes
            CheckDlgButton(hwnd, IDC_CLICK_THROUGH_CHECK, g_current_prefs->config.click_through ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_ALWAYS_TOP_CHECK, g_current_prefs->config.always_on_top ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_USE_CUSTOM_SIZE_CHECK, g_current_prefs->config.use_custom_size ? BST_CHECKED : BST_UNCHECKED);

            // Hotkey
            SetDlgItemText(hwnd, IDC_HOTKEY_EDIT, g_current_prefs->config.hotkey);

            // Custom size
            char buf[32];
            sprintf(buf, "%d", g_current_prefs->config.custom_width_px);
            SetDlgItemText(hwnd, IDC_WIDTH_EDIT, buf);
            sprintf(buf, "%d", g_current_prefs->config.custom_height_px);
            SetDlgItemText(hwnd, IDC_HEIGHT_EDIT, buf);

            return TRUE;
        }

        case WM_HSCROLL: {
            if ((HWND)lParam == GetDlgItem(hwnd, IDC_SCALE_SLIDER)) {
                int pos = SendDlgItemMessage(hwnd, IDC_SCALE_SLIDER, TBM_GETPOS, 0, 0);
                char buf[16];
                sprintf(buf, "%d%%", pos);
                SetDlgItemText(hwnd, IDC_SCALE_LABEL, buf);
            } else if ((HWND)lParam == GetDlgItem(hwnd, IDC_OPACITY_SLIDER)) {
                int pos = SendDlgItemMessage(hwnd, IDC_OPACITY_SLIDER, TBM_GETPOS, 0, 0);
                char buf[16];
                sprintf(buf, "%d%%", pos);
                SetDlgItemText(hwnd, IDC_OPACITY_LABEL, buf);
            }
            break;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_APPLY_BUTTON: {
                    // Read values from controls
                    int scale_pos = SendDlgItemMessage(hwnd, IDC_SCALE_SLIDER, TBM_GETPOS, 0, 0);
                    g_current_prefs->config.scale = scale_pos / 100.0f;

                    int opacity_pos = SendDlgItemMessage(hwnd, IDC_OPACITY_SLIDER, TBM_GETPOS, 0, 0);
                    g_current_prefs->config.opacity = opacity_pos / 100.0f;

                    int auto_hide_idx = SendDlgItemMessage(hwnd, IDC_AUTO_HIDE_COMBO, CB_GETCURSEL, 0, 0);
                    switch (auto_hide_idx) {
                        case 0: g_current_prefs->config.auto_hide = 0.0f; break;
                        case 1: g_current_prefs->config.auto_hide = 0.8f; break;
                        case 2: g_current_prefs->config.auto_hide = 2.0f; break;
                    }

                    g_current_prefs->config.position_mode = SendDlgItemMessage(hwnd, IDC_POSITION_COMBO, CB_GETCURSEL, 0, 0);
                    g_current_prefs->config.click_through = IsDlgButtonChecked(hwnd, IDC_CLICK_THROUGH_CHECK) == BST_CHECKED;
                    g_current_prefs->config.always_on_top = IsDlgButtonChecked(hwnd, IDC_ALWAYS_TOP_CHECK) == BST_CHECKED;

                    char hotkey_buf[128];
                    GetDlgItemText(hwnd, IDC_HOTKEY_EDIT, hotkey_buf, sizeof(hotkey_buf));
                    strncpy(g_current_prefs->config.hotkey, hotkey_buf, sizeof(g_current_prefs->config.hotkey) - 1);

                    g_current_prefs->config.use_custom_size = IsDlgButtonChecked(hwnd, IDC_USE_CUSTOM_SIZE_CHECK) == BST_CHECKED;
                    if (g_current_prefs->config.use_custom_size) {
                        char width_buf[32], height_buf[32];
                        GetDlgItemText(hwnd, IDC_WIDTH_EDIT, width_buf, sizeof(width_buf));
                        GetDlgItemText(hwnd, IDC_HEIGHT_EDIT, height_buf, sizeof(height_buf));
                        g_current_prefs->config.custom_width_px = atoi(width_buf);
                        g_current_prefs->config.custom_height_px = atoi(height_buf);
                    }

                    if (g_current_prefs->apply_callback) {
                        g_current_prefs->apply_callback(g_current_prefs->config);
                    }

                    SetDlgItemText(hwnd, IDC_ERROR_LABEL, "");
                    break;
                }

                case IDC_CLOSE_BUTTON:
                    EndDialog(hwnd, IDCANCEL);
                    break;

                case IDC_RESTORE_BUTTON:
                    g_current_prefs->config = get_default_config();
                    // Re-initialize dialog with defaults
                    SendDlgItemMessage(hwnd, IDC_SCALE_SLIDER, TBM_SETPOS, TRUE, (int)(g_current_prefs->config.scale * 100));
                    SendDlgItemMessage(hwnd, IDC_OPACITY_SLIDER, TBM_SETPOS, TRUE, (int)(g_current_prefs->config.opacity * 100));
                    SendDlgItemMessage(hwnd, IDC_AUTO_HIDE_COMBO, CB_SETCURSEL, 0, 0);
                    SendDlgItemMessage(hwnd, IDC_POSITION_COMBO, CB_SETCURSEL, g_current_prefs->config.position_mode, 0);
                    CheckDlgButton(hwnd, IDC_CLICK_THROUGH_CHECK, BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_ALWAYS_TOP_CHECK, BST_UNCHECKED);
                    CheckDlgButton(hwnd, IDC_USE_CUSTOM_SIZE_CHECK, BST_UNCHECKED);
                    SetDlgItemText(hwnd, IDC_HOTKEY_EDIT, g_current_prefs->config.hotkey);
                    break;
            }
            break;
        }

        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            break;
    }
    return FALSE;
}

PreferencesDialog* create_preferences_dialog(HINSTANCE hInstance, Config config, void (*apply_callback)(Config)) {
    PreferencesDialog *prefs = calloc(1, sizeof(PreferencesDialog));
    if (!prefs) return NULL;

    prefs->config = config;
    prefs->original_config = config;
    prefs->apply_callback = apply_callback;

    return prefs;
}

void destroy_preferences_dialog(PreferencesDialog* prefs) {
    if (!prefs) return;
    free(prefs);
}

BOOL show_preferences_dialog(PreferencesDialog* prefs, HINSTANCE hInstance, HWND parent_hwnd) {
    if (!prefs) return FALSE;

    // Note: In a real implementation, you'd create a dialog resource
    // For now, we'll use a simple message box as placeholder
    MessageBox(parent_hwnd, "Preferences dialog would be shown here", "Preferences", MB_OK);

    return TRUE;
}

void update_preferences_config(PreferencesDialog* prefs, Config new_config) {
    if (!prefs) return;
    prefs->config = new_config;
}
