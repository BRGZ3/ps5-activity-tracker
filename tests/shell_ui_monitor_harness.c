#include "shell_ui_monitor.h"

#include <assert.h>

static pid_t fake_pid = 100;
static int fake_result;

int
shell_ui_monitor_query_pid(pid_t *output) {
    if(fake_result != 0) return fake_result;
    *output = fake_pid;
    return 0;
}

int
main(void) {
    shell_ui_monitor_t monitor;
    pid_t old_pid = 0;
    pid_t new_pid = 0;

    shell_ui_monitor_init(&monitor);
    assert(shell_ui_monitor_poll(&monitor, 1000, &old_pid, &new_pid) == 0);
    assert(shell_ui_monitor_pid(&monitor) == 100);

    fake_pid = 200;
    assert(shell_ui_monitor_poll(&monitor, 1500, &old_pid, &new_pid) == 0);
    assert(shell_ui_monitor_poll(&monitor, 2000, &old_pid, &new_pid) == 1);
    assert(old_pid == 100);
    assert(new_pid == 200);

    fake_pid = 0;
    assert(shell_ui_monitor_poll(&monitor, 3000, &old_pid, &new_pid) == 1);
    assert(old_pid == 200);
    assert(new_pid == 0);

    fake_result = -1;
    assert(shell_ui_monitor_poll(&monitor, 4000, &old_pid, &new_pid) == -1);
    assert(shell_ui_monitor_pid(&monitor) == 0);

    fake_result = 0;
    fake_pid = 300;
    assert(shell_ui_monitor_poll(&monitor, 5000, &old_pid, &new_pid) == 1);
    assert(old_pid == 0);
    assert(new_pid == 300);
    return 0;
}
