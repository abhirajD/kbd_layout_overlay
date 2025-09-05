#ifndef THREADING_H
#define THREADING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Thread handle (opaque)
typedef struct ThreadHandle ThreadHandle;

// Mutex handle (opaque)
typedef struct MutexHandle MutexHandle;

// Condition variable handle (opaque)
typedef struct ConditionHandle ConditionHandle;

// Semaphore handle (opaque)
typedef struct SemaphoreHandle SemaphoreHandle;

// Thread function type
typedef void (*ThreadFunction)(void* user_data);

// Threading system interface
typedef struct {
    // Thread management
    ThreadHandle* (*create_thread)(ThreadFunction function, void* user_data, const char* name);
    void (*destroy_thread)(ThreadHandle* thread);
    bool (*join_thread)(ThreadHandle* thread, int timeout_ms);
    bool (*detach_thread)(ThreadHandle* thread);
    void (*sleep_thread)(uint32_t milliseconds);
    void (*yield_thread)(void);
    uint32_t (*get_thread_id)(void);
    bool (*set_thread_priority)(ThreadHandle* thread, int priority);
    int (*get_thread_priority)(ThreadHandle* thread);

    // Mutex operations
    MutexHandle* (*create_mutex)(void);
    void (*destroy_mutex)(MutexHandle* mutex);
    bool (*lock_mutex)(MutexHandle* mutex, int timeout_ms);
    bool (*try_lock_mutex)(MutexHandle* mutex);
    void (*unlock_mutex)(MutexHandle* mutex);

    // Condition variables
    ConditionHandle* (*create_condition)(void);
    void (*destroy_condition)(ConditionHandle* condition);
    bool (*wait_condition)(ConditionHandle* condition, MutexHandle* mutex, int timeout_ms);
    void (*signal_condition)(ConditionHandle* condition);
    void (*broadcast_condition)(ConditionHandle* condition);

    // Semaphore operations
    SemaphoreHandle* (*create_semaphore)(int initial_count, int max_count);
    void (*destroy_semaphore)(SemaphoreHandle* semaphore);
    bool (*wait_semaphore)(SemaphoreHandle* semaphore, int timeout_ms);
    bool (*try_wait_semaphore)(SemaphoreHandle* semaphore);
    void (*post_semaphore)(SemaphoreHandle* semaphore);
    int (*get_semaphore_value)(SemaphoreHandle* semaphore);

    // Atomic operations
    int32_t (*atomic_increment)(volatile int32_t* value);
    int32_t (*atomic_decrement)(volatile int32_t* value);
    int32_t (*atomic_add)(volatile int32_t* value, int32_t addend);
    int32_t (*atomic_exchange)(volatile int32_t* value, int32_t new_value);
    bool (*atomic_compare_exchange)(volatile int32_t* value, int32_t expected, int32_t new_value);

    // Thread-local storage
    uintptr_t (*create_tls_key)(void);
    void (*delete_tls_key)(uintptr_t key);
    void (*set_tls_value)(uintptr_t key, void* value);
    void* (*get_tls_value)(uintptr_t key);
} ThreadingSystem;

// Thread priority levels
typedef enum {
    THREAD_PRIORITY_LOWEST = -2,
    THREAD_PRIORITY_LOW = -1,
    THREAD_PRIORITY_NORMAL = 0,
    THREAD_PRIORITY_HIGH = 1,
    THREAD_PRIORITY_HIGHEST = 2
} ThreadPriority;

// Get platform-specific threading system implementation
ThreadingSystem* get_threading_system(void);

// High-level utilities built on top of the threading system
typedef struct {
    ThreadHandle* handle;
    ThreadFunction function;
    void* user_data;
    char name[64];
} Thread;

// Simplified thread creation
Thread* create_named_thread(const char* name, ThreadFunction function, void* user_data);
void destroy_thread(Thread* thread);
bool join_thread(Thread* thread, int timeout_ms);

// Simplified mutex wrapper
typedef struct {
    MutexHandle* handle;
} Mutex;

Mutex* create_mutex(void);
void destroy_mutex(Mutex* mutex);
void lock_mutex(Mutex* mutex);
bool try_lock_mutex(Mutex* mutex);
void unlock_mutex(Mutex* mutex);

// RAII-style mutex lock
typedef struct {
    Mutex* mutex;
} ScopedLock;

ScopedLock* create_scoped_lock(Mutex* mutex);
void destroy_scoped_lock(ScopedLock* lock);

#ifdef __cplusplus
}
#endif

#endif // THREADING_H
