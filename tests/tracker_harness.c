#include "tracker.h"

int
main(void) {
    const unsigned long long start = 1785225600000ULL;
    tracker_init(start);
    tracker_event("metadata", "PPSA02177", "Test Game", start);
    tracker_event("foreground", "PPSA02177", 0, start);
    tracker_tick(start + 60000);
    tracker_event("background", "PPSA02177", 0, start + 60000);
    tracker_event("foreground", "PPSA02177", 0, start + 80000);
    tracker_event("exit", "PPSA02177", 0, start + 90000);
    tracker_set_completed("PPSA02177", 1, start + 90000);
    tracker_diagnostic("error", "launch_error", "PPSA02177",
                       "launch failed", start + 91000);
    tracker_diagnostic("error", "launch_error", "PPSA02177",
                       "launch failed", start + 92000);
    tracker_set_config(180, "Europe/Moscow", "4.50", start + 92500);
    tracker_shutdown(start + 93000);
    tracker_init(start + 120000);
    tracker_shutdown(start + 120000);
    return 0;
}
