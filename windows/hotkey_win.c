#include <windows.h>
#include "../shared/app_context.h"
#include "../shared/hotkey.h"
#include "../shared/error.h"

/* Windows implementation for shared hotkey adapter.
   This only handles RegisterHotKey/UnregisterHotKey. Low-level hook
   management (SetWindowsHookEx) remains in windows/main.c via win_hook_install/uninstall
   to preserve the existing activation semantics.
*/

klo_error_t klo_hotkey_register(klo_hotkey_context_t *hk, klo_window_handle_t window) {
    KLO_CHECK_PARAM(hk);
    /* If already registered, nothing to do */
    if (hk->is_registered) return KLO_OK;

    UINT modifiers = (UINT)hk->modifiers;
    UINT vkey = (UINT)hk->virtual_key;

    if (!RegisterHotKey((HWND)window, 1, modifiers | MOD_NOREPEAT, vkey)) {
        KLO_ERROR_RETURN(KLO_ERR_HOTKEY_REGISTER, "RegisterHotKey failed");
    }
    hk->is_registered = 1;
    return KLO_OK;
}

klo_error_t klo_hotkey_unregister(klo_hotkey_context_t *hk) {
    KLO_CHECK_PARAM(hk);
    if (!hk->is_registered) return KLO_OK;

    if (!UnregisterHotKey(NULL, 1)) {
        KLO_ERROR_RETURN(KLO_ERR_HOTKEY_UNREGISTER, "UnregisterHotKey failed");
    }
    hk->is_registered = 0;
    return KLO_OK;
}
