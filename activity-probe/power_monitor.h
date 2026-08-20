#ifndef PLAYLOG_POWER_MONITOR_H
#define PLAYLOG_POWER_MONITOR_H

#include <stdint.h>

typedef struct power_monitor {
    intptr_t info_handle;
    intptr_t status_handle;
    uint64_t last_info;
    uint64_t last_status;
    int info_open;
    int status_open;
    int has_info;
    int has_status;
    int transition_reported;
    const char *pending_reason;
} power_monitor_t;

int power_monitor_open(power_monitor_t *monitor);
int power_monitor_poll(power_monitor_t *monitor, const char **reason);
void power_monitor_close(power_monitor_t *monitor);

#endif
