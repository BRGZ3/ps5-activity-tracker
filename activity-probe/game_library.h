#ifndef PS5_ACTIVITY_GAME_LIBRARY_H
#define PS5_ACTIVITY_GAME_LIBRARY_H

#include <stddef.h>

#define GAME_LIBRARY_TITLE_ID_SIZE 10
#define GAME_LIBRARY_NAME_SIZE 256
#define GAME_LIBRARY_VERSION_SIZE 64
#define GAME_LIBRARY_PATH_SIZE 512

typedef struct game_library_entry {
    char title_id[GAME_LIBRARY_TITLE_ID_SIZE];
    char name[GAME_LIBRARY_NAME_SIZE];
    char version[GAME_LIBRARY_VERSION_SIZE];
    char metadata_path[GAME_LIBRARY_PATH_SIZE];
} game_library_entry_t;

int game_library_collect(game_library_entry_t *entries, size_t capacity,
                         size_t *count);

/* Returns whether the current filesystem still contains the title.  The
 * database is a historical registry on some firmware versions, so a row in
 * app.db alone is not proof that the game is installed now. */
int game_library_entry_is_installed(const game_library_entry_t *entry);

/* State from the most recent collection attempt, exposed read-only for
 * diagnosing an empty library on the console. */
const char *game_library_source(void);
const char *game_library_database_status(void);
unsigned game_library_database_rows(void);

#endif
