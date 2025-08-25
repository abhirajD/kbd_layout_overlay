#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Simple cross-platform file logger used for debugging hotkey events and lifecycle.
   API:
     - logger_init(): open log file (called early on startup)
     - logger_log(fmt, ...): append a timestamped message
     - logger_close(): flush/close file

   Log path:
     - Windows: %LOCALAPPDATA%/kbd_layout_overlay.log (falls back to ./kbd_layout_overlay.log)
     - macOS/Linux: /tmp/kbd_layout_overlay.log
*/

void logger_init(void);
void logger_close(void);
void logger_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
