#ifndef PS5_ACTIVITY_SHELL_UI_MONITOR_H
#define PS5_ACTIVITY_SHELL_UI_MONITOR_H

#include <stdint.h>
#include <sys/types.h>

typedef struct shell_ui_monitor {
    pid_t last_pid;
    uint64_t next_poll_ms;
    int has_sample;
} shell_ui_monitor_t;

void shell_ui_monitor_init(shell_ui_monitor_t *monitor);
int shell_ui_monitor_poll(shell_ui_monitor_t *monitor, uint64_t now_ms,
                          pid_t *old_pid, pid_t *new_pid);
pid_t shell_ui_monitor_pid(const shell_ui_monitor_t *monitor);

#endif
