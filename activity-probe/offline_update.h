#ifndef PS5_ACTIVITY_OFFLINE_UPDATE_H
#define PS5_ACTIVITY_OFFLINE_UPDATE_H

#include <stddef.h>

#define OFFLINE_UPDATE_STATUS_SIZE 512

int offline_update_status_json(char *output, size_t output_size);
int offline_update_apply(char *output, size_t output_size);
int offline_setup_status_json(char *output, size_t output_size);
int offline_setup_install(const char *mode, char *output, size_t output_size);
int offline_ensure_autoload_entry(const char *list_path,
                                  const char *entry_name);
int offline_remove_autoload_entry(const char *list_path,
                                  const char *entry_name);

#endif
