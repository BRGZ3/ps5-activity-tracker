#include "power_monitor.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint64_t info_pattern = 1000;
static uint64_t status_pattern = 4;
static int info_poll_result;
static int status_poll_result;
static int close_count;

int
sceKernelOpenEventFlag(intptr_t *event_flag, const char *name) {
    if(strcmp(name, "SceSystemStateMgrInfo") == 0) {
        *event_flag = 41;
        return 0;
    }
    if(strcmp(name, "SceSystemStateMgrStatus") == 0) {
        *event_flag = 42;
        return 0;
    }
    return -1;
}

int
sceKernelPollEventFlag(intptr_t event_flag, uint64_t bit_pattern,
                       unsigned int wait_mode, uint64_t *result_pattern) {
    assert(bit_pattern == UINT64_MAX);
    assert(wait_mode == 2);
    if(event_flag == 41) {
        if(info_poll_result < 0) return info_poll_result;
        *result_pattern = info_pattern;
        return 0;
    }
    assert(event_flag == 42);
    if(status_poll_result < 0) return status_poll_result;
    *result_pattern = status_pattern;
    return 0;
}

int
sceKernelCloseEventFlag(intptr_t event_flag) {
    assert(event_flag == 41 || event_flag == 42);
    close_count++;
    return 0;
}

int
main(void) {
    power_monitor_t monitor;
    const char *reason = NULL;

    assert(power_monitor_open(&monitor) == 0);
    assert(power_monitor_poll(&monitor, &reason) == 0);
    status_pattern |= 0x200000;
    assert(power_monitor_poll(&monitor, &reason) == 1);
    assert(strcmp(reason,
                  "SceSystemStateMgrStatus=SHELLUI_SHUTDOWN_IN_PROGRESS")
           == 0);
    assert(power_monitor_poll(&monitor, &reason) == 0);
    power_monitor_close(&monitor);

    status_pattern = 4;
    info_pattern = 1000;
    assert(power_monitor_open(&monitor) == 0);
    info_poll_result = -1;
    assert(power_monitor_poll(&monitor, &reason) == 0);
    info_poll_result = 0;
    info_pattern = 500;
    assert(power_monitor_poll(&monitor, &reason) == 1);
    assert(strcmp(reason, "SceSystemStateMgrInfo=MAIN_ON_STANDBY") == 0);
    power_monitor_close(&monitor);

    info_pattern = 300;
    assert(power_monitor_open(&monitor) == 0);
    assert(power_monitor_poll(&monitor, &reason) == 1);
    assert(strcmp(reason, "SceSystemStateMgrInfo=SUSPEND_ON_GOING") == 0);
    power_monitor_close(&monitor);

    assert(close_count == 6);
    return 0;
}
