#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Timer handle (opaque)
typedef struct TimerHandle TimerHandle;

// Timer callback function type
typedef void (*TimerCallback)(void* user_data);

// Timer system interface
typedef struct {
    // Timer management
    TimerHandle* (*create_timer)(TimerCallback callback, void* user_data, uint32_t interval_ms, bool repeating);
    void (*destroy_timer)(TimerHandle* timer);
    bool (*start_timer)(TimerHandle* timer);
    bool (*stop_timer)(TimerHandle* timer);
    bool (*reset_timer)(TimerHandle* timer);
    bool (*is_timer_running)(TimerHandle* timer);

    // Timer properties
    bool (*set_timer_interval)(TimerHandle* timer, uint32_t interval_ms);
    uint32_t (*get_timer_interval)(TimerHandle* timer);
    bool (*set_timer_repeating)(TimerHandle* timer, bool repeating);
    bool (*get_timer_repeating)(TimerHandle* timer);

    // High-precision timing
    uint64_t (*get_current_time_microseconds)(void);
    uint64_t (*get_current_time_milliseconds)(void);
    void (*sleep_microseconds)(uint64_t microseconds);
    void (*sleep_milliseconds)(uint32_t milliseconds);

    // Performance timing
    uint64_t (*get_performance_counter)(void);
    uint64_t (*get_performance_frequency)(void);
    double (*get_performance_elapsed_seconds)(uint64_t start, uint64_t end);

    // Date and time
    bool (*get_current_date_time)(int* year, int* month, int* day, int* hour, int* minute, int* second, int* millisecond);
    uint64_t (*get_unix_timestamp)(void);
    void (*format_timestamp)(uint64_t timestamp, char* buffer, size_t buffer_size, const char* format);
} TimerSystem;

// Get platform-specific timer system implementation
TimerSystem* get_timer_system(void);

// High-level timer utilities
typedef struct {
    TimerHandle* handle;
    TimerCallback callback;
    void* user_data;
    uint32_t interval_ms;
    bool repeating;
    char name[64];
} NamedTimer;

// Simplified timer creation
NamedTimer* create_named_timer(const char* name, TimerCallback callback, void* user_data,
                              uint32_t interval_ms, bool repeating);
void destroy_named_timer(NamedTimer* timer);
bool start_named_timer(NamedTimer* timer);
bool stop_named_timer(NamedTimer* timer);

// One-shot timer utilities
bool schedule_delayed_callback(const char* name, TimerCallback callback, void* user_data, uint32_t delay_ms);
bool cancel_delayed_callback(const char* name);

// Performance measurement utilities
typedef struct {
    uint64_t start_time;
    const char* name;
} PerformanceTimer;

PerformanceTimer* start_performance_timer(const char* name);
void end_performance_timer(PerformanceTimer* timer);
double get_performance_timer_elapsed_ms(PerformanceTimer* timer);

// Frame rate measurement
typedef struct {
    uint64_t frame_count;
    uint64_t last_time;
    double fps;
    double frame_time_ms;
} FrameRateCounter;

FrameRateCounter* create_frame_rate_counter(void);
void destroy_frame_rate_counter(FrameRateCounter* counter);
void update_frame_rate_counter(FrameRateCounter* counter);
double get_frame_rate(const FrameRateCounter* counter);
double get_frame_time_ms(const FrameRateCounter* counter);

// Utility functions
uint64_t milliseconds_to_microseconds(uint32_t milliseconds);
uint32_t microseconds_to_milliseconds(uint64_t microseconds);
double microseconds_to_seconds(uint64_t microseconds);
uint64_t seconds_to_microseconds(double seconds);

#ifdef __cplusplus
}
#endif

#endif // TIMER_H
