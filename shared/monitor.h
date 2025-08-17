#ifndef MONITOR_H
#define MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x, y;           /* Top-left corner */
    int width, height;  /* Dimensions */
    int is_primary;     /* 1 if primary monitor, 0 otherwise */
} MonitorInfo;

/* Get monitor count */
int get_monitor_count(void);

/* Get info for specific monitor (1-based index) */
int get_monitor_info(int monitor_index, MonitorInfo *info);

/* Get monitor that contains the active/focused window */
int get_active_monitor(MonitorInfo *info);

/* Get primary monitor info */
int get_primary_monitor(MonitorInfo *info);

#ifdef __cplusplus
}
#endif

#endif /* MONITOR_H */