#include "error.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Global error state */
static klo_error_context_t g_last_error = {KLO_OK, NULL, NULL, 0, NULL};
static klo_error_handler_t g_error_handler = NULL;
static klo_log_level_t g_log_level = KLO_LOG_INFO;

/* Static error messages */
static char g_error_message_buffer[256] = {0};

void klo_error_init(klo_error_handler_t handler) {
    g_error_handler = handler;
    klo_error_clear();
}

void klo_error_set(klo_error_t code, const char *message, const char *file, int line, const char *function) {
    g_last_error.code = code;
    g_last_error.file = file;
    g_last_error.line = line;
    g_last_error.function = function;
    
    /* Copy message to our buffer to ensure it persists */
    if (message) {
        strncpy(g_error_message_buffer, message, sizeof(g_error_message_buffer) - 1);
        g_error_message_buffer[sizeof(g_error_message_buffer) - 1] = '\0';
        g_last_error.message = g_error_message_buffer;
    } else {
        g_last_error.message = klo_error_string(code);
    }
    
    /* Call platform-specific error handler if set */
    if (g_error_handler && code != KLO_OK) {
        g_error_handler(&g_last_error);
    }
}

klo_error_t klo_error_get_code(void) {
    return g_last_error.code;
}

const char *klo_error_get_message(void) {
    return g_last_error.message ? g_last_error.message : "No error message available";
}

void klo_error_clear(void) {
    g_last_error.code = KLO_OK;
    g_last_error.message = NULL;
    g_last_error.file = NULL;
    g_last_error.line = 0;
    g_last_error.function = NULL;
    g_error_message_buffer[0] = '\0';
}

const char *klo_error_string(klo_error_t code) {
    switch (code) {
        case KLO_OK:
            return "Success";
        
        /* Configuration errors */
        case KLO_ERR_CONFIG_INVALID:
            return "Invalid configuration parameter";
        case KLO_ERR_CONFIG_IO:
            return "Configuration file I/O error";
        case KLO_ERR_CONFIG_MEMORY:
            return "Memory allocation failed in configuration";
        
        /* Overlay errors */
        case KLO_ERR_OVERLAY_NOT_FOUND:
            return "Overlay image file not found";
        case KLO_ERR_OVERLAY_DECODE:
            return "Failed to decode overlay image";
        case KLO_ERR_OVERLAY_MEMORY:
            return "Memory allocation failed for overlay";
        case KLO_ERR_OVERLAY_INVALID:
            return "Invalid overlay parameters";
        
        /* Monitor errors */
        case KLO_ERR_MONITOR_INVALID:
            return "Invalid monitor index";
        case KLO_ERR_MONITOR_NOT_FOUND:
            return "Monitor not found";
        case KLO_ERR_MONITOR_QUERY:
            return "Failed to query monitor information";
        
        /* Hotkey errors */
        case KLO_ERR_HOTKEY_INVALID:
            return "Invalid hotkey string";
        case KLO_ERR_HOTKEY_REGISTER:
            return "Failed to register hotkey";
        case KLO_ERR_HOTKEY_UNREGISTER:
            return "Failed to unregister hotkey";
        
        /* Platform errors */
        case KLO_ERR_PLATFORM_INIT:
            return "Platform initialization failed";
        case KLO_ERR_PLATFORM_WINDOW:
            return "Window creation/management failed";
        case KLO_ERR_PLATFORM_GRAPHICS:
            return "Graphics operations failed";
        
        /* Generic errors */
        case KLO_ERR_MEMORY:
            return "Memory allocation failed";
        case KLO_ERR_INVALID_PARAM:
            return "Invalid parameter";
        case KLO_ERR_NOT_INITIALIZED:
            return "Component not properly initialized";
        case KLO_ERR_INTERNAL:
            return "Internal error";
        
        default:
            return "Unknown error";
    }
}

void klo_log(klo_log_level_t level, const char *format, ...) {
    if (level < g_log_level) {
        return;
    }
    
    const char *level_str;
    switch (level) {
        case KLO_LOG_DEBUG: level_str = "DEBUG"; break;
        case KLO_LOG_INFO:  level_str = "INFO";  break;
        case KLO_LOG_WARN:  level_str = "WARN";  break;
        case KLO_LOG_ERROR: level_str = "ERROR"; break;
        case KLO_LOG_FATAL: level_str = "FATAL"; break;
        default:            level_str = "LOG";   break;
    }
    
    printf("[%s] ", level_str);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
}

/* Set minimum log level for filtering */
void klo_set_log_level(klo_log_level_t level) {
    g_log_level = level;
}