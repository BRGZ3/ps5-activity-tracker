#include "tracker.h"

int
main(void) {
    const unsigned long long start = 1785272340000ULL;
    tracker_init(start);
    tracker_event("metadata", "CUSA00001", "Midnight Game", start);
    tracker_event("foreground", "CUSA00001", 0, start);
    tracker_tick(start + 120000);
    tracker_event("exit", "CUSA00001", 0, start + 120000);
    tracker_shutdown(start + 120000);
    return 0;
}
