#include "tracker.h"

#include <stdio.h>

int
main(void) {
    const unsigned long long start = 1785362400000ULL;
    char backup_id[64];
    tracker_init(start);
    tracker_event("metadata", "PPSA02177", "Backup Game", start + 500);
    tracker_event("foreground", "PPSA02177", "Backup Game", start + 1000);
    tracker_tick(start + 61000);
    if(tracker_backup_create(start + 62000, backup_id,
                             sizeof(backup_id)) != 0) return 1;
    tracker_event("metadata", "CUSA03048", "Later Game", start + 62500);
    tracker_event("foreground", "CUSA03048", "Later Game", start + 63000);
    tracker_tick(start + 123000);
    if(tracker_backup_restore(backup_id, start + 124000) != 0) return 2;
    printf("%s\n", backup_id);
    return 0;
}
