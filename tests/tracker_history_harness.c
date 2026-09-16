#include "tracker.h"

#include <unistd.h>

int
main(void) {
    const unsigned long long start = 1785362400000ULL;
    int seed = access(TRACKER_STATE_PATH, F_OK) != 0;
    tracker_init(start);
    if(seed) {
        tracker_event("metadata", "PPSA02177", "History Game", start + 500);
        tracker_event("foreground", "PPSA02177", NULL, start + 1000);
        tracker_tick(start + 61000);
        tracker_event("exit", "PPSA02177", NULL, start + 61000);
        tracker_set_completed("PPSA02177", 1, start + 62000);
    }
    tracker_shutdown(start + 63000);
    return 0;
}
