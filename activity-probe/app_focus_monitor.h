#ifndef APP_FOCUS_MONITOR_H
#define APP_FOCUS_MONITOR_H

#include <stdint.h>

typedef struct app_focus_monitor {
    intptr_t handle;
    uint32_t last_app_id;
    int is_open;
    int has_last_app_id;
} app_focus_monitor_t;

int app_focus_monitor_open(app_focus_monitor_t *monitor);
int app_focus_monitor_poll(app_focus_monitor_t *monitor,
                           uint32_t *old_app_id, uint32_t *new_app_id);
void app_focus_monitor_close(app_focus_monitor_t *monitor);

#endif
