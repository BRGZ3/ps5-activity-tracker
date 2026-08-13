#include "app_focus_monitor.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint64_t fake_pattern = 7;
static int fake_poll_result;
static int close_count;

int
sceKernelOpenEventFlag(intptr_t *event_flag, const char *name) {
    assert(strcmp(name, "SceShellCoreUtilAppFocus") == 0);
    *event_flag = 42;
    return 0;
}

int
sceKernelPollEventFlag(intptr_t event_flag, uint64_t bit_pattern,
                       unsigned int wait_mode, uint64_t *result_pattern) {
    assert(event_flag == 42);
    assert(bit_pattern == UINT64_MAX);
    assert(wait_mode == 2);
    if(fake_poll_result < 0) return fake_poll_result;
    *result_pattern = fake_pattern;
    return 0;
}

int
sceKernelCloseEventFlag(intptr_t event_flag) {
    assert(event_flag == 42);
    close_count++;
    return 0;
}

int
main(void) {
    app_focus_monitor_t monitor;
    uint32_t old_app_id = 0;
    uint32_t new_app_id = 0;

    assert(app_focus_monitor_open(&monitor) == 0);
    assert(monitor.last_app_id == 7);
    assert(app_focus_monitor_poll(&monitor, &old_app_id, &new_app_id) == 0);

    fake_pattern = 0x18;
    assert(app_focus_monitor_poll(&monitor, &old_app_id, &new_app_id) == 1);
    assert(old_app_id == 7);
    assert(new_app_id == 0x18);
    assert(app_focus_monitor_poll(&monitor, &old_app_id, &new_app_id) == 0);

    fake_pattern = 7;
    assert(app_focus_monitor_poll(&monitor, &old_app_id, &new_app_id) == 1);
    assert(old_app_id == 0x18);
    assert(new_app_id == 7);

    fake_poll_result = -1;
    assert(app_focus_monitor_poll(&monitor, &old_app_id, &new_app_id) == 0);
    fake_poll_result = 0;
    fake_pattern = 0x2018;
    assert(app_focus_monitor_poll(&monitor, &old_app_id, &new_app_id) == 1);
    assert(old_app_id == 7);
    assert(new_app_id == 0x2018);

    app_focus_monitor_close(&monitor);
    assert(close_count == 1);

    fake_poll_result = -1;
    assert(app_focus_monitor_open(&monitor) == 0);
    assert(!monitor.has_last_app_id);
    fake_poll_result = 0;
    fake_pattern = 7;
    assert(app_focus_monitor_poll(&monitor, &old_app_id, &new_app_id) == 1);
    assert(old_app_id == 0);
    assert(new_app_id == 7);
    app_focus_monitor_close(&monitor);
    assert(close_count == 2);
    return 0;
}
