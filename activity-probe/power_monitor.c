#include "power_monitor.h"

#include <stdint.h>
#include <string.h>

#define EVENT_FLAG_ALL_BITS UINT64_MAX
#define EVENT_FLAG_WAITMODE_OR 2u
#define SYSTEM_STATUS_SHUTDOWN_IN_PROGRESS 0x0000000000200000ULL

#define SYSTEM_STATE_SHUTDOWN_ON_GOING 100u
#define SYSTEM_STATE_SUSPEND_ON_GOING 300u
#define SYSTEM_STATE_MAIN_ON_STANDBY 500u

int sceKernelOpenEventFlag(intptr_t *event_flag, const char *name);
int sceKernelPollEventFlag(intptr_t event_flag, uint64_t bit_pattern,
                           unsigned int wait_mode, uint64_t *result_pattern);
int sceKernelCloseEventFlag(intptr_t event_flag);

static int
poll_flag(intptr_t handle, uint64_t *pattern) {
    return sceKernelPollEventFlag(
        handle, EVENT_FLAG_ALL_BITS, EVENT_FLAG_WAITMODE_OR, pattern);
}

static int
dangerous_info_state(uint64_t pattern, const char **reason) {
    unsigned state = (unsigned)(pattern & 0xffffu);
    if(state == SYSTEM_STATE_SHUTDOWN_ON_GOING) {
        *reason = "SceSystemStateMgrInfo=SHUTDOWN_ON_GOING";
        return 1;
    }
    if(state == SYSTEM_STATE_MAIN_ON_STANDBY) {
        *reason = "SceSystemStateMgrInfo=MAIN_ON_STANDBY";
        return 1;
    }
    if(state == SYSTEM_STATE_SUSPEND_ON_GOING) {
        *reason = "SceSystemStateMgrInfo=SUSPEND_ON_GOING";
        return 1;
    }
    return 0;
}

int
power_monitor_open(power_monitor_t *monitor) {
    uint64_t pattern;
    if(!monitor) return -1;
    memset(monitor, 0, sizeof(*monitor));
    monitor->info_handle = -1;
    monitor->status_handle = -1;
    if(sceKernelOpenEventFlag(
           &monitor->info_handle, "SceSystemStateMgrInfo") == 0) {
        monitor->info_open = 1;
        if(poll_flag(monitor->info_handle, &pattern) == 0) {
            monitor->last_info = pattern;
            monitor->has_info = 1;
            dangerous_info_state(pattern, &monitor->pending_reason);
        }
    }
    if(sceKernelOpenEventFlag(
           &monitor->status_handle, "SceSystemStateMgrStatus") == 0) {
        monitor->status_open = 1;
        if(poll_flag(monitor->status_handle, &pattern) == 0) {
            monitor->last_status = pattern;
            monitor->has_status = 1;
            if((pattern & SYSTEM_STATUS_SHUTDOWN_IN_PROGRESS) != 0) {
                monitor->pending_reason =
                    "SceSystemStateMgrStatus=SHELLUI_SHUTDOWN_IN_PROGRESS";
            }
        }
    }
    return monitor->info_open || monitor->status_open ? 0 : -1;
}

int
power_monitor_poll(power_monitor_t *monitor, const char **reason) {
    uint64_t pattern;
    const char *detected_reason = NULL;
    if(reason) *reason = NULL;
    if(!monitor || monitor->transition_reported) return 0;
    if(monitor->pending_reason) {
        monitor->transition_reported = 1;
        if(reason) *reason = monitor->pending_reason;
        monitor->pending_reason = NULL;
        return 1;
    }

    if(monitor->status_open
       && poll_flag(monitor->status_handle, &pattern) == 0) {
        int entered = (pattern & SYSTEM_STATUS_SHUTDOWN_IN_PROGRESS) != 0
            && (!monitor->has_status
                || (monitor->last_status
                    & SYSTEM_STATUS_SHUTDOWN_IN_PROGRESS) == 0);
        monitor->last_status = pattern;
        monitor->has_status = 1;
        if(entered) {
            detected_reason =
                "SceSystemStateMgrStatus=SHELLUI_SHUTDOWN_IN_PROGRESS";
        }
    }

    if(!detected_reason && monitor->info_open
       && poll_flag(monitor->info_handle, &pattern) == 0) {
        unsigned previous =
            monitor->has_info ? (unsigned)(monitor->last_info & 0xffffu) : 0;
        unsigned current = (unsigned)(pattern & 0xffffu);
        monitor->last_info = pattern;
        monitor->has_info = 1;
        if(current != previous) {
            dangerous_info_state(pattern, &detected_reason);
        }
    }

    if(!detected_reason) return 0;
    monitor->transition_reported = 1;
    if(reason) *reason = detected_reason;
    return 1;
}

void
power_monitor_close(power_monitor_t *monitor) {
    if(!monitor) return;
    if(monitor->status_open) sceKernelCloseEventFlag(monitor->status_handle);
    if(monitor->info_open) sceKernelCloseEventFlag(monitor->info_handle);
    monitor->status_handle = -1;
    monitor->info_handle = -1;
    monitor->status_open = 0;
    monitor->info_open = 0;
}
