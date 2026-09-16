#include "shell_ui_monitor.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SHELL_UI_POLL_INTERVAL_MS 1000ULL

#ifdef __FreeBSD__
#include <sys/sysctl.h>
#include <sys/user.h>

static int
query_shell_ui_pid(pid_t *output) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0;
    unsigned char *buffer;
    unsigned char *cursor;
    *output = 0;
    if(sysctl(mib, 4, NULL, &size, NULL, 0) != 0 || size == 0) return -1;
    buffer = malloc(size);
    if(!buffer) return -1;
    if(sysctl(mib, 4, buffer, &size, NULL, 0) != 0) {
        free(buffer);
        return -1;
    }
    for(cursor = buffer; cursor + sizeof(struct kinfo_proc) <= buffer + size;) {
        struct kinfo_proc *process = (struct kinfo_proc *)cursor;
        if(process->ki_structsize < (int)sizeof(struct kinfo_proc)
           || cursor + process->ki_structsize > buffer + size) {
            break;
        }
        if(strcasecmp(process->ki_comm, "SceShellUI") == 0) {
            *output = process->ki_pid;
            break;
        }
        cursor += process->ki_structsize;
    }
    free(buffer);
    return 0;
}
#else
int shell_ui_monitor_query_pid(pid_t *output);

static int
query_shell_ui_pid(pid_t *output) {
    return shell_ui_monitor_query_pid(output);
}
#endif

void
shell_ui_monitor_init(shell_ui_monitor_t *monitor) {
    if(!monitor) return;
    memset(monitor, 0, sizeof(*monitor));
}

int
shell_ui_monitor_poll(shell_ui_monitor_t *monitor, uint64_t now_ms,
                      pid_t *old_pid, pid_t *new_pid) {
    pid_t current;
    if(!monitor || !old_pid || !new_pid) return -1;
    if(monitor->next_poll_ms && now_ms < monitor->next_poll_ms) return 0;
    monitor->next_poll_ms = now_ms + SHELL_UI_POLL_INTERVAL_MS;
    if(query_shell_ui_pid(&current) != 0) return -1;
    if(!monitor->has_sample) {
        monitor->last_pid = current;
        monitor->has_sample = 1;
        return 0;
    }
    if(current == monitor->last_pid) return 0;
    *old_pid = monitor->last_pid;
    *new_pid = current;
    monitor->last_pid = current;
    return 1;
}

pid_t
shell_ui_monitor_pid(const shell_ui_monitor_t *monitor) {
    return monitor && monitor->has_sample ? monitor->last_pid : 0;
}
