#ifndef HOTKEY_PARSE_H
#define HOTKEY_PARSE_H

#include <Carbon/Carbon.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse a human-readable hotkey string (e.g. "Command+Option+Slash")
   into Carbon modifier flags and a virtual key code.
   Returns 1 on success, 0 on failure (invalid format / missing parts). */
int parse_hotkey_string(const char *hk, UInt32 *out_modifiers, UInt32 *out_vk);

#ifdef __cplusplus
}
#endif

#endif /* HOTKEY_PARSE_H */
