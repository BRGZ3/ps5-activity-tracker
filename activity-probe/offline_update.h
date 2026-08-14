#ifndef PS5_ACTIVITY_OFFLINE_UPDATE_H
#define PS5_ACTIVITY_OFFLINE_UPDATE_H

#include <stddef.h>

#define OFFLINE_UPDATE_STATUS_SIZE 512
#define OFFLINE_AUTOLOAD_CONFIGURED 0
#define OFFLINE_AUTOLOAD_MANUAL_REQUIRED 1
#define OFFLINE_RUNTIME_MANUAL_REQUIRED 1

int offline_update_status_json(char *output, size_t output_size);
int offline_update_apply(char *output, size_t output_size);
int offline_setup_status_json(char *output, size_t output_size);
int offline_setup_install(const char *mode, char *output, size_t output_size);
int offline_ensure_autoload_entry(const char *list_path,
                                  const char *entry_name);
int offline_remove_autoload_entry(const char *list_path,
                                  const char *entry_name);
int offline_deploy_runtime_copies(const char *source_path, const char *mode,
                                  unsigned *copies_written,
                                  int *manual_autoload_required);

#endif
