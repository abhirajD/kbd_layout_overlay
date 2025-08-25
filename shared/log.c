#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

static FILE *g_log_file = NULL;

/* Determine platform-appropriate path and open append */
void logger_init(void) {
    if (g_log_file) return;

#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");
    char path[1024];
    if (local && local[0] != '\0') {
        /* Put file at %LOCALAPPDATA%\kbd_layout_overlay.log */
        snprintf(path, sizeof(path), "%s\\kbd_layout_overlay.log", local);
    } else {
        strncpy(path, "kbd_layout_overlay.log", sizeof(path));
        path[sizeof(path)-1] = '\0';
    }
    g_log_file = fopen(path, "a");
#else
    const char *path = "/tmp/kbd_layout_overlay.log";
    g_log_file = fopen(path, "a");
#endif

    if (!g_log_file) {
        /* best-effort fallback to current directory */
        g_log_file = fopen("kbd_layout_overlay.log", "a");
    }
    if (g_log_file) {
        time_t t = time(NULL);
        struct tm ltbuf;
        struct tm *lt = NULL;
#ifdef _WIN32
        if (localtime_s(&ltbuf, &t) == 0) lt = &ltbuf;
#else
        lt = localtime_r(&t, &ltbuf);
#endif
        if (lt) {
            fprintf(g_log_file, "---- Logger started: %04d-%02d-%02d %02d:%02d:%02d ----\n",
                    lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                    lt->tm_hour, lt->tm_min, lt->tm_sec);
            fflush(g_log_file);
        }
    }
}

void logger_close(void) {
    if (g_log_file) {
        fprintf(g_log_file, "---- Logger closed ----\n");
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void logger_log(const char *fmt, ...) {
    if (!g_log_file) return;
    time_t t = time(NULL);
    struct tm ltbuf;
    struct tm *lt = NULL;
#ifdef _WIN32
    if (localtime_s(&ltbuf, &t) == 0) lt = &ltbuf;
#else
    lt = localtime_r(&t, &ltbuf);
#endif
    if (lt) {
        fprintf(g_log_file, "[%04d-%02d-%02d %02d:%02d:%02d] ",
                lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                lt->tm_hour, lt->tm_min, lt->tm_sec);
    } else {
        fprintf(g_log_file, "[unknown time] ");
    }

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log_file, fmt, ap);
    va_end(ap);

    fprintf(g_log_file, "\n");
    fflush(g_log_file);
}
