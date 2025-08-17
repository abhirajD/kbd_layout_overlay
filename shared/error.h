#ifndef ERROR_H
#define ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Standardized error codes for the entire application */
typedef enum {
    KLO_OK = 0,                    /* Success */
    
    /* Configuration errors */
    KLO_ERR_CONFIG_INVALID = -1001,   /* Invalid configuration parameter */
    KLO_ERR_CONFIG_IO = -1002,        /* Configuration file I/O error */
    KLO_ERR_CONFIG_MEMORY = -1003,    /* Memory allocation failed in config */
    
    /* Overlay/Image errors */
    KLO_ERR_OVERLAY_NOT_FOUND = -2001,   /* Overlay image file not found */
    KLO_ERR_OVERLAY_DECODE = -2002,      /* Failed to decode image */
    KLO_ERR_OVERLAY_MEMORY = -2003,      /* Memory allocation failed for overlay */
    KLO_ERR_OVERLAY_INVALID = -2004,     /* Invalid overlay parameters */
    
    /* Monitor errors */
    KLO_ERR_MONITOR_INVALID = -3001,     /* Invalid monitor index */
    KLO_ERR_MONITOR_NOT_FOUND = -3002,   /* Monitor not found */
    KLO_ERR_MONITOR_QUERY = -3003,       /* Failed to query monitor info */
    
    /* Hotkey errors */
    KLO_ERR_HOTKEY_INVALID = -4001,      /* Invalid hotkey string */
    KLO_ERR_HOTKEY_REGISTER = -4002,     /* Failed to register hotkey */
    KLO_ERR_HOTKEY_UNREGISTER = -4003,   /* Failed to unregister hotkey */
    
    /* Platform-specific errors */
    KLO_ERR_PLATFORM_INIT = -5001,       /* Platform initialization failed */
    KLO_ERR_PLATFORM_WINDOW = -5002,     /* Window creation/management failed */
    KLO_ERR_PLATFORM_GRAPHICS = -5003,   /* Graphics operations failed */
    
    /* Generic errors */
    KLO_ERR_MEMORY = -9001,               /* Generic memory allocation failure */
    KLO_ERR_INVALID_PARAM = -9002,       /* Invalid parameter passed to function */
    KLO_ERR_NOT_INITIALIZED = -9003,     /* Component not properly initialized */
    KLO_ERR_INTERNAL = -9999             /* Internal/unexpected error */
} klo_error_t;

/* Error severity levels */
typedef enum {
    KLO_LOG_DEBUG = 0,
    KLO_LOG_INFO = 1,
    KLO_LOG_WARN = 2,
    KLO_LOG_ERROR = 3,
    KLO_LOG_FATAL = 4
} klo_log_level_t;

/* Error context for detailed error reporting */
typedef struct {
    klo_error_t code;
    const char *message;
    const char *file;
    int line;
    const char *function;
} klo_error_context_t;

/* Function pointer for platform-specific error display */
typedef void (*klo_error_handler_t)(const klo_error_context_t *ctx);

/* Initialize error system with platform-specific handler */
void klo_error_init(klo_error_handler_t handler);

/* Set error with context information */
void klo_error_set(klo_error_t code, const char *message, const char *file, int line, const char *function);

/* Get last error code */
klo_error_t klo_error_get_code(void);

/* Get last error message */
const char *klo_error_get_message(void);

/* Clear last error */
void klo_error_clear(void);

/* Convert error code to human-readable string */
const char *klo_error_string(klo_error_t code);

/* Log message with specified level */
void klo_log(klo_log_level_t level, const char *format, ...);

/* Set minimum log level for filtering */
void klo_set_log_level(klo_log_level_t level);

/* Convenience macros for error reporting */
#define KLO_ERROR(code, msg) klo_error_set(code, msg, __FILE__, __LINE__, __func__)
#define KLO_ERROR_RETURN(code, msg) do { KLO_ERROR(code, msg); return code; } while(0)
#define KLO_CHECK_PARAM(param) do { if (!(param)) { KLO_ERROR_RETURN(KLO_ERR_INVALID_PARAM, "Invalid parameter: " #param); } } while(0)
#define KLO_CHECK_MEMORY(ptr) do { if (!(ptr)) { KLO_ERROR_RETURN(KLO_ERR_MEMORY, "Memory allocation failed"); } } while(0)

#ifdef __cplusplus
}
#endif

#endif /* ERROR_H */