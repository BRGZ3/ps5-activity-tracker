#ifndef PS5_ACTIVITY_TRACKER_H
#define PS5_ACTIVITY_TRACKER_H

#include <stdint.h>

#ifndef TRACKER_DATA_DIR
#define TRACKER_DATA_DIR "/data/ps5-activity"
#endif
#define TRACKER_SUMMARY_PATH TRACKER_DATA_DIR "/summary.json"
#define TRACKER_STATE_PATH TRACKER_DATA_DIR "/tracker-state.bin"
#define TRACKER_PREVIOUS_STATE_PATH TRACKER_DATA_DIR "/tracker-state.prev.bin"
#define TRACKER_COMPLETED_PATH TRACKER_DATA_DIR "/completed-state.bin"
#define TRACKER_DIAGNOSTICS_PATH TRACKER_DATA_DIR "/diagnostics-state.bin"
#define TRACKER_CONFIG_PATH TRACKER_DATA_DIR "/config.json"
#define TRACKER_BACKUPS_DIR TRACKER_DATA_DIR "/backups"
#define TRACKER_BACKUPS_INDEX_PATH TRACKER_DATA_DIR "/backups.json"

int tracker_init(uint64_t realtime_ms);
void tracker_event(const char *event, const char *title_id,
                   const char *title_name, uint64_t realtime_ms);
void tracker_tick(uint64_t realtime_ms);
void tracker_shutdown(uint64_t realtime_ms);
int tracker_set_completed(const char *title_id, int completed,
                          uint64_t realtime_ms);
int tracker_set_config(int timezone_offset, const char *timezone,
                       const char *firmware, uint64_t realtime_ms);
int tracker_backup_create(uint64_t realtime_ms, char *backup_id,
                          unsigned backup_id_size);
int tracker_backup_restore(const char *backup_id, uint64_t realtime_ms);
int tracker_backup_delete(const char *backup_id);
int tracker_backup_refresh_index(void);
void tracker_diagnostic(const char *level, const char *type,
                        const char *title_id, const char *message,
                        uint64_t realtime_ms);

#endif
