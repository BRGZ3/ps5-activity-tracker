#include "http_server.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
tracker_set_completed(const char *title_id, int completed,
                      unsigned long long realtime_ms) {
    (void)title_id;
    (void)completed;
    (void)realtime_ms;
    return 0;
}

int
tracker_set_config(int timezone_offset, const char *timezone,
                   const char *firmware, unsigned long long realtime_ms) {
    (void)timezone_offset;
    (void)timezone;
    (void)firmware;
    (void)realtime_ms;
    return 0;
}

int
tracker_backup_create(unsigned long long realtime_ms, char *backup_id,
                      unsigned backup_id_size) {
    (void)realtime_ms;
    snprintf(backup_id, backup_id_size, "backup-20260730-120000");
    return 0;
}

int
tracker_backup_restore(const char *backup_id,
                       unsigned long long realtime_ms) {
    (void)realtime_ms;
    return strcmp(backup_id, "backup-20260730-120000") == 0 ? 0 : -1;
}

int
tracker_backup_delete(const char *backup_id) {
    return strcmp(backup_id, "backup-20260730-120000") == 0 ? 0 : -1;
}

int
tracker_backup_refresh_index(void) {
    return 0;
}

int
offline_update_status_json(char *output, size_t output_size) {
    snprintf(output, output_size,
             "{\"ok\":true,\"available\":false}\n");
    return 0;
}

int
offline_update_apply(char *output, size_t output_size) {
    snprintf(output, output_size,
             "{\"ok\":true,\"restart_required\":true}\n");
    return 0;
}

int
offline_setup_status_json(char *output, size_t output_size) {
    snprintf(output, output_size,
             "{\"ok\":true,\"installed\":false}\n");
    return 0;
}

int
offline_setup_install(const char *mode, char *output, size_t output_size) {
    if(strcmp(mode, "etahen") != 0 && strcmp(mode, "autoloader") != 0) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"invalid_mode\"}\n");
        return -1;
    }
    snprintf(output, output_size,
             "{\"ok\":true,\"mode\":\"%s\",\"restart_required\":true}\n",
             mode);
    return 0;
}

int
main(void) {
    if(dashboard_http_start() != 0) return 1;
    puts("ready");
    fflush(stdout);
    sleep(3);
    dashboard_http_stop();
    return 0;
}
