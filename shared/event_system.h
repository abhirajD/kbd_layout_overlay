#ifndef EVENT_SYSTEM_H
#define EVENT_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Cross-platform event types
typedef enum {
    EVENT_TYPE_KEY_DOWN,
    EVENT_TYPE_KEY_UP,
    EVENT_TYPE_MOUSE_DOWN,
    EVENT_TYPE_MOUSE_UP,
    EVENT_TYPE_MOUSE_MOVE,
    EVENT_TYPE_WINDOW_RESIZE,
    EVENT_TYPE_WINDOW_CLOSE,
    EVENT_TYPE_HOTKEY,
    EVENT_TYPE_TIMER,
    EVENT_TYPE_CUSTOM
} EventType;

// Cross-platform key codes (subset of common keys)
typedef enum {
    KEY_UNKNOWN = 0,
    KEY_A = 65, KEY_B = 66, KEY_C = 67,
    KEY_SPACE = 32,
    KEY_ENTER = 13,
    KEY_ESCAPE = 27,
    KEY_F1 = 112, KEY_F2 = 113, KEY_F3 = 114, KEY_F4 = 115,
    KEY_F5 = 116, KEY_F6 = 117, KEY_F7 = 118, KEY_F8 = 119,
    KEY_F9 = 120, KEY_F10 = 121, KEY_F11 = 122, KEY_F12 = 123,
    KEY_LEFT = 37, KEY_UP = 38, KEY_RIGHT = 39, KEY_DOWN = 40,
    KEY_LEFT_SHIFT = 16, KEY_LEFT_CTRL = 17, KEY_LEFT_ALT = 18,
    KEY_RIGHT_SHIFT = 161, KEY_RIGHT_CTRL = 163, KEY_RIGHT_ALT = 165
} KeyCode;

// Cross-platform modifiers
typedef enum {
    MODIFIER_NONE = 0,
    MODIFIER_SHIFT = 1 << 0,
    MODIFIER_CTRL = 1 << 1,
    MODIFIER_ALT = 1 << 2,
    MODIFIER_CMD = 1 << 3,  // Mac command key
    MODIFIER_WIN = 1 << 4   // Windows key
} KeyModifier;

// Generic event structure
typedef struct {
    EventType type;
    uint32_t timestamp;
    union {
        struct {
            KeyCode key;
            KeyModifier modifiers;
            bool repeat;
        } key;
        struct {
            int x, y;
            int button;
            KeyModifier modifiers;
        } mouse;
        struct {
            int width, height;
        } resize;
        struct {
            uint32_t hotkey_id;
        } hotkey;
        struct {
            uint32_t timer_id;
        } timer;
        struct {
            uint32_t custom_type;
            void* data;
            size_t data_size;
        } custom;
    } data;
} Event;

// Event callback function type
typedef void (*EventCallback)(const Event* event, void* user_data);

// Event system interface
typedef struct {
    bool (*init)(void);
    void (*shutdown)(void);
    bool (*register_callback)(EventType type, EventCallback callback, void* user_data);
    bool (*unregister_callback)(EventType type, EventCallback callback);
    bool (*post_event)(const Event* event);
    bool (*process_events)(void);
    bool (*wait_for_event)(int timeout_ms);
} EventSystem;

// Get platform-specific event system implementation
EventSystem* get_event_system(void);

// Utility functions for key code conversion
KeyCode platform_key_to_generic(uint32_t platform_key);
uint32_t generic_key_to_platform(KeyCode generic_key);

// Utility functions for modifier conversion
KeyModifier platform_modifiers_to_generic(uint32_t platform_modifiers);
uint32_t generic_modifiers_to_platform(KeyModifier generic_modifiers);

#ifdef __cplusplus
}
#endif

#endif // EVENT_SYSTEM_H
