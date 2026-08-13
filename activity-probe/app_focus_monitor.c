#include "app_focus_monitor.h"

#include <stdint.h>
#include <string.h>

#define APP_FOCUS_FLAG_NAME "SceShellCoreUtilAppFocus"
#define EVENT_FLAG_ALL_BITS UINT64_MAX
#define EVENT_FLAG_WAITMODE_OR 2u

int sceKernelOpenEventFlag(intptr_t *event_flag, const char *name);
int sceKernelPollEventFlag(intptr_t event_flag, uint64_t bit_pattern,
                           unsigned int wait_mode, uint64_t *result_pattern);
int sceKernelCloseEventFlag(intptr_t event_flag);

static int
read_app_id(app_focus_monitor_t *monitor, uint32_t *app_id) {
    uint64_t pattern = 0;
    int result = sceKernelPollEventFlag(
        monitor->handle, EVENT_FLAG_ALL_BITS, EVENT_FLAG_WAITMODE_OR, &pattern);
    if(result < 0 || pattern == 0 || (pattern >> 32) != 0) {
        return result < 0 ? result : -1;
    }
    *app_id = (uint32_t)pattern;
    return 0;
}

int
app_focus_monitor_open(app_focus_monitor_t *monitor) {
    uint32_t app_id;
    int result;

    if(!monitor) return -1;
    memset(monitor, 0, sizeof(*monitor));
    monitor->handle = -1;
    result = sceKernelOpenEventFlag(&monitor->handle, APP_FOCUS_FLAG_NAME);
    if(result < 0) return result;
    monitor->is_open = 1;
    result = read_app_id(monitor, &app_id);
    if(result == 0) {
        monitor->last_app_id = app_id;
        monitor->has_last_app_id = 1;
    }
    return 0;
}

int
app_focus_monitor_poll(app_focus_monitor_t *monitor,
                       uint32_t *old_app_id, uint32_t *new_app_id) {
    uint32_t current_app_id;
    int result;

    if(!monitor || !monitor->is_open || !old_app_id || !new_app_id) return -1;
    result = read_app_id(monitor, &current_app_id);
    if(result < 0) return 0;
    if(!monitor->has_last_app_id) {
        monitor->last_app_id = current_app_id;
        monitor->has_last_app_id = 1;
        *old_app_id = 0;
        *new_app_id = current_app_id;
        return 1;
    }
    if(current_app_id == monitor->last_app_id) return 0;
    *old_app_id = monitor->last_app_id;
    *new_app_id = current_app_id;
    monitor->last_app_id = current_app_id;
    return 1;
}

void
app_focus_monitor_close(app_focus_monitor_t *monitor) {
    if(!monitor || !monitor->is_open) return;
    sceKernelCloseEventFlag(monitor->handle);
    monitor->handle = -1;
    monitor->is_open = 0;
    monitor->has_last_app_id = 0;
}
