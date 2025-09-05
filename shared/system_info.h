#ifndef SYSTEM_INFO_H
#define SYSTEM_INFO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// System information structure
typedef struct {
    char os_name[64];
    char os_version[64];
    char architecture[32];
    char hostname[256];
    char username[256];
    uint64_t total_memory_mb;
    uint64_t available_memory_mb;
    int cpu_count;
    int cpu_cores;
    char cpu_name[128];
    char gpu_name[128];
    int screen_count;
    struct {
        int width, height;
        int refresh_rate;
        float scale_factor;
    } screens[8];
} SystemInfo;

// Power state information
typedef struct {
    bool is_battery_powered;
    int battery_percentage;
    bool is_charging;
    int time_remaining_minutes;
} PowerInfo;

// Network information
typedef struct {
    bool is_online;
    char ip_address[64];
    char mac_address[32];
    uint64_t bytes_sent;
    uint64_t bytes_received;
} NetworkInfo;

// Storage information
typedef struct {
    char mount_point[256];
    char filesystem[32];
    uint64_t total_bytes;
    uint64_t available_bytes;
    uint64_t used_bytes;
} StorageInfo;

// System information interface
typedef struct {
    // Basic system info
    bool (*get_system_info)(SystemInfo* info);
    uint64_t (*get_uptime_seconds)(void);
    uint64_t (*get_process_memory_usage)(void);
    double (*get_cpu_usage)(void);

    // Power management
    bool (*get_power_info)(PowerInfo* info);
    bool (*prevent_sleep)(bool prevent);
    bool (*prevent_display_sleep)(bool prevent);

    // Network information
    bool (*get_network_info)(NetworkInfo* info);
    bool (*get_storage_info)(StorageInfo* info_array, size_t max_count, size_t* count);

    // System capabilities
    bool (*is_64bit_system)(void);
    bool (*has_touch_support)(void);
    bool (*has_pen_support)(void);
    bool (*has_accelerometer)(void);
    bool (*has_gyroscope)(void);

    // Hardware information
    const char* (*get_processor_name)(void);
    int (*get_processor_count)(void);
    uint64_t (*get_total_memory)(void);
    uint64_t (*get_available_memory)(void);

    // Display information
    int (*get_display_count)(void);
    bool (*get_display_info)(int display_index, int* width, int* height, float* scale_factor);
    bool (*get_primary_display_info)(int* width, int* height, float* scale_factor);

    // System paths
    const char* (*get_temp_directory)(void);
    const char* (*get_home_directory)(void);
    const char* (*get_app_data_directory)(void);
    const char* (*get_executable_path)(void);
} SystemInfoAPI;

// Get platform-specific system information implementation
SystemInfoAPI* get_system_info_api(void);

// Utility functions
void print_system_info(const SystemInfo* info);
void print_power_info(const PowerInfo* info);
void print_network_info(const NetworkInfo* info);
void print_storage_info(const StorageInfo* info);

// Memory information utilities
uint64_t bytes_to_mb(uint64_t bytes);
uint64_t mb_to_bytes(uint64_t mb);
const char* format_bytes(uint64_t bytes, char* buffer, size_t size);

// CPU information utilities
double get_current_process_cpu_usage(void);
double get_system_cpu_usage(void);

// Process information
uint32_t get_current_process_id(void);
uint32_t get_current_thread_id(void);
uint64_t get_current_process_memory_usage(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_INFO_H
