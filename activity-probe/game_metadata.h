#ifndef PS5_ACTIVITY_GAME_METADATA_H
#define PS5_ACTIVITY_GAME_METADATA_H

#include <stddef.h>

int game_metadata_is_installed(const char *title_id);
int game_metadata_cache_icon(const char *title_id);
int game_metadata_find_icon(const char *title_id, char *output,
                            size_t output_size);

#endif
