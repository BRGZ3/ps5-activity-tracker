#include "game_metadata.h"
#include "tracker.h"

#include <ctype.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef USER_APPMETA_DIR
#define USER_APPMETA_DIR "/user/appmeta"
#endif
#ifndef SYSTEM_APPMETA_DIR
#define SYSTEM_APPMETA_DIR "/system_data/priv/appmeta"
#endif
#ifndef SYSTEM_MMS_APPMETA_DIR
#define SYSTEM_MMS_APPMETA_DIR "/system_data/priv/mms/appmeta"
#endif
#ifndef USER_APP_DIR
#define USER_APP_DIR "/user/app"
#endif
#ifndef SYSTEM_APP_DIR
#define SYSTEM_APP_DIR "/system_ex/app"
#endif
#ifndef APP_DB_PATH
#define APP_DB_PATH "/system_data/priv/mms/app.db"
#endif

#define COVERS_DIR TRACKER_DATA_DIR "/covers"
#define MAX_ICON_SIZE (16 * 1024 * 1024)
#define MAX_ICON_PATH 1024

static int
valid_title_id(const char *title_id) {
    if(!title_id || strlen(title_id) != 9
       || (strncmp(title_id, "CUSA", 4) != 0
           && strncmp(title_id, "PPSA", 4) != 0)) {
        return 0;
    }
    for(size_t i = 4; i < 9; i++) {
        if(title_id[i] < '0' || title_id[i] > '9') return 0;
    }
    return 1;
}

static int
regular_file(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode)
        && info.st_size > 0 && info.st_size <= MAX_ICON_SIZE;
}

static int
copy_icon_path(const char *candidate, char *output, size_t output_size) {
    char path[MAX_ICON_PATH];
    const char *begin;
    size_t length;
    if(!candidate || !candidate[0]) return -1;
    /* app.db rows are normally absolute paths, but firmware versions and
     * mounted-image tools have also emitted whitespace, quotes, URI prefixes
     * and cache fragments around icon0.png.  Normalize those harmless forms
     * before applying the path/type checks below. */
    begin = candidate;
    while(*begin && isspace((unsigned char)*begin)) begin++;
    length = strlen(begin);
    while(length > 0 && isspace((unsigned char)begin[length - 1])) length--;
    if(length >= 2 && begin[0] == '"' && begin[length - 1] == '"') {
        begin++;
        length -= 2;
    }
    if(length >= 7 && strncmp(begin, "file://", 7) == 0) {
        begin += 7;
        length -= 7;
    }
    {
        size_t cut = length;
        for(size_t i = 0; i < length; i++) {
            if(begin[i] == '?' || begin[i] == '#') {
                cut = i;
                break;
            }
        }
        length = cut;
    }
    while(length > 0 && isspace((unsigned char)begin[length - 1])) length--;
    if(length == 0 || length >= sizeof(path)) return -1;
    memcpy(path, begin, length);
    path[length] = '\0';
    /* app.db is trusted metadata, but keep the LAN icon endpoint constrained
     * to absolute PNG files.  This prevents a malformed registry row from
     * turning the read-only server into an arbitrary-file reader. */
    if(path[0] != '/') return -1;
    {
        const char *extension = strrchr(path, '.');
        if(!extension || (strcmp(extension, ".png") != 0
                          && strcmp(extension, ".PNG") != 0)) return -1;
    }
    if(!regular_file(path)) return -1;
    if(!output || output_size == 0
       || snprintf(output, output_size, "%s", path) >= (int)output_size) {
        return -1;
    }
    return 0;
}

static int
find_live_icon(const char *title_id, char *output, size_t output_size) {
    static const char *formats[] = {
        USER_APPMETA_DIR "/%s/icon0.png",
        SYSTEM_APPMETA_DIR "/%s/icon0.png",
        "/system_data/priv/mms/appmeta/%s/icon0.png",
        "/system_data/priv/mms/appmeta/%s/sce_sys/icon0.png",
        USER_APP_DIR "/%s/icon0.png",
        USER_APP_DIR "/%s/sce_sys/icon0.png",
        SYSTEM_APP_DIR "/%s/icon0.png",
        SYSTEM_APP_DIR "/%s/sce_sys/icon0.png",
        "/data/homebrew/%s/icon0.png",
        "/data/homebrew/%s/sce_sys/icon0.png",
        "/data/etaHEN/games/%s/icon0.png",
        "/data/etaHEN/games/%s/sce_sys/icon0.png",
        "/data/%s/icon0.png",
        "/data/%s/sce_sys/icon0.png",
        "/mnt/ext0/homebrew/%s/icon0.png",
        "/mnt/ext0/homebrew/%s/sce_sys/icon0.png",
        "/mnt/ext0/etaHEN/games/%s/icon0.png",
        "/mnt/ext0/etaHEN/games/%s/sce_sys/icon0.png",
        "/mnt/ext1/homebrew/%s/icon0.png",
        "/mnt/ext1/homebrew/%s/sce_sys/icon0.png",
        "/mnt/ext1/etaHEN/games/%s/icon0.png",
        "/mnt/ext1/etaHEN/games/%s/sce_sys/icon0.png",
        "/mnt/usb0/homebrew/%s/icon0.png",
        "/mnt/usb0/homebrew/%s/sce_sys/icon0.png",
        "/mnt/usb1/homebrew/%s/icon0.png",
        "/mnt/usb1/homebrew/%s/sce_sys/icon0.png",
        "/mnt/usb2/homebrew/%s/icon0.png",
        "/mnt/usb2/homebrew/%s/sce_sys/icon0.png",
        "/mnt/usb3/homebrew/%s/icon0.png",
        "/mnt/usb3/homebrew/%s/sce_sys/icon0.png",
        "/mnt/usb4/homebrew/%s/icon0.png",
        "/mnt/usb4/homebrew/%s/sce_sys/icon0.png",
        "/mnt/usb5/homebrew/%s/icon0.png",
        "/mnt/usb5/homebrew/%s/sce_sys/icon0.png",
        "/mnt/usb6/homebrew/%s/icon0.png",
        "/mnt/usb6/homebrew/%s/sce_sys/icon0.png",
        "/mnt/usb7/homebrew/%s/icon0.png",
        "/mnt/usb7/homebrew/%s/sce_sys/icon0.png",
        "/mnt/usb0/etaHEN/games/%s/icon0.png",
        "/mnt/usb0/etaHEN/games/%s/sce_sys/icon0.png",
        "/mnt/usb1/etaHEN/games/%s/icon0.png",
        "/mnt/usb1/etaHEN/games/%s/sce_sys/icon0.png",
        "/mnt/usb2/etaHEN/games/%s/icon0.png",
        "/mnt/usb2/etaHEN/games/%s/sce_sys/icon0.png",
        "/mnt/usb3/etaHEN/games/%s/icon0.png",
        "/mnt/usb3/etaHEN/games/%s/sce_sys/icon0.png",
        "/mnt/usb4/etaHEN/games/%s/icon0.png",
        "/mnt/usb4/etaHEN/games/%s/sce_sys/icon0.png",
        "/mnt/usb5/etaHEN/games/%s/icon0.png",
        "/mnt/usb5/etaHEN/games/%s/sce_sys/icon0.png",
        "/mnt/usb6/etaHEN/games/%s/icon0.png",
        "/mnt/usb6/etaHEN/games/%s/sce_sys/icon0.png",
        "/mnt/usb7/etaHEN/games/%s/icon0.png",
        "/mnt/usb7/etaHEN/games/%s/sce_sys/icon0.png",
        "/mnt/usb0/%s/icon0.png",
        "/mnt/usb0/%s/sce_sys/icon0.png",
        "/mnt/usb1/%s/icon0.png",
        "/mnt/usb1/%s/sce_sys/icon0.png",
        "/mnt/usb2/%s/icon0.png",
        "/mnt/usb2/%s/sce_sys/icon0.png",
        "/mnt/usb3/%s/icon0.png",
        "/mnt/usb3/%s/sce_sys/icon0.png",
        "/mnt/usb4/%s/icon0.png",
        "/mnt/usb4/%s/sce_sys/icon0.png",
        "/mnt/usb5/%s/icon0.png",
        "/mnt/usb5/%s/sce_sys/icon0.png",
        "/mnt/usb6/%s/icon0.png",
        "/mnt/usb6/%s/sce_sys/icon0.png",
        "/mnt/usb7/%s/icon0.png",
        "/mnt/usb7/%s/sce_sys/icon0.png",
        "/mnt/ext0/%s/icon0.png",
        "/mnt/ext0/%s/sce_sys/icon0.png",
        "/mnt/ext1/%s/icon0.png",
        "/mnt/ext1/%s/sce_sys/icon0.png"
    };
    for(size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        int written = snprintf(output, output_size, formats[i], title_id);
        if(written > 0 && (size_t)written < output_size
           && regular_file(output)) return 0;
    }
    return -1;
}

/* The shell's registry contains the authoritative icon path for both
 * installed and image-backed titles.  It is often suffixed with a cache
 * timestamp ("?ts=...") and the mounted content may not be reachable through
 * one of the conventional /user/app* paths, so use the registry as a final
 * local, read-only fallback. */
static int
find_database_icon(const char *title_id, char *output, size_t output_size) {
    static const char *queries[] = {
        "SELECT icon0Info, metaDataPath FROM tbl_contentinfo "
        "WHERE upper(trim(titleId)) = upper(?1);",
        "SELECT NULL AS icon0Info, metaDataPath FROM tbl_contentinfo "
        "WHERE upper(trim(titleId)) = upper(?1);"
    };
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int result = -1;
    if(!valid_title_id(title_id) || !output || output_size == 0) return -1;
    if(sqlite3_open_v2(APP_DB_PATH, &database, SQLITE_OPEN_READONLY, NULL)
       != SQLITE_OK) {
        if(database) sqlite3_close(database);
        return -1;
    }
    (void)sqlite3_busy_timeout(database, 500);
    for(size_t query_index = 0;
        query_index < sizeof(queries) / sizeof(queries[0]); query_index++) {
        if(sqlite3_prepare_v2(database, queries[query_index], -1,
                              &statement, NULL) != SQLITE_OK) {
            if(statement) {
                sqlite3_finalize(statement);
                statement = NULL;
            }
            continue;
        }
        if(sqlite3_bind_text(statement, 1, title_id, -1, SQLITE_TRANSIENT)
           != SQLITE_OK) {
            sqlite3_finalize(statement);
            statement = NULL;
            continue;
        }
        while(sqlite3_step(statement) == SQLITE_ROW) {
            const unsigned char *icon = sqlite3_column_text(statement, 0);
            const unsigned char *metadata = sqlite3_column_text(statement, 1);
            char candidate[MAX_ICON_PATH];
            if(copy_icon_path((const char *)icon, output, output_size) == 0) {
                result = 0;
            } else if(metadata && metadata[0]) {
                if(copy_icon_path((const char *)metadata, output,
                                  output_size) == 0) {
                    result = 0;
                } else if(snprintf(candidate, sizeof(candidate), "%s/icon0.png",
                                   (const char *)metadata)
                              < (int)sizeof(candidate)
                          && copy_icon_path(candidate, output, output_size)
                                 == 0) {
                    result = 0;
                } else if(snprintf(candidate, sizeof(candidate),
                                   "%s/sce_sys/icon0.png",
                                   (const char *)metadata)
                              < (int)sizeof(candidate)
                          && copy_icon_path(candidate, output, output_size)
                                 == 0) {
                    result = 0;
                }
            }
            if(result == 0) break;
        }
        sqlite3_finalize(statement);
        statement = NULL;
        if(result == 0) break;
    }
    if(statement) sqlite3_finalize(statement);
    sqlite3_close(database);
    return result;
}

static int
find_any_icon(const char *title_id, char *output, size_t output_size) {
    if(find_live_icon(title_id, output, output_size) == 0) return 0;
    return find_database_icon(title_id, output, output_size);
}

static int
copy_file_atomic(const char *source, const char *destination) {
    char temporary[512];
    unsigned char *buffer;
    int input;
    int output;
    int failed;
    ssize_t count;
    if(snprintf(temporary, sizeof(temporary), "%s.tmp", destination)
       >= (int)sizeof(temporary)) return -1;
    input = open(source, O_RDONLY);
    if(input < 0) return -1;
    output = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(output < 0) {
        close(input);
        return -1;
    }
    buffer = malloc(8192);
    if(!buffer) {
        close(input);
        close(output);
        unlink(temporary);
        return -1;
    }
    while((count = read(input, buffer, 8192)) > 0) {
        ssize_t offset = 0;
        while(offset < count) {
            ssize_t written = write(
                output, buffer + offset, (size_t)(count - offset));
            if(written <= 0) {
                free(buffer);
                close(input);
                close(output);
                unlink(temporary);
                return -1;
            }
            offset += written;
        }
    }
    close(input);
    failed = count < 0 || fsync(output) != 0;
    if(close(output) != 0) failed = 1;
    free(buffer);
    if(failed || rename(temporary, destination) != 0) {
        unlink(temporary);
        return -1;
    }
    chmod(destination, 0644);
    return 0;
}

int
game_metadata_is_installed(const char *title_id) {
    static const char *formats[] = {
        USER_APPMETA_DIR "/%s",
        SYSTEM_APPMETA_DIR "/%s",
        SYSTEM_MMS_APPMETA_DIR "/%s",
        USER_APP_DIR "/%s",
        SYSTEM_APP_DIR "/%s",
        "/data/homebrew/%s",
        "/data/etaHEN/games/%s",
        "/data/%s",
        "/mnt/ext0/homebrew/%s",
        "/mnt/ext0/etaHEN/games/%s",
        "/mnt/ext1/homebrew/%s",
        "/mnt/ext1/etaHEN/games/%s",
        "/mnt/usb0/homebrew/%s",
        "/mnt/usb1/homebrew/%s",
        "/mnt/usb2/homebrew/%s",
        "/mnt/usb3/homebrew/%s",
        "/mnt/usb4/homebrew/%s",
        "/mnt/usb5/homebrew/%s",
        "/mnt/usb6/homebrew/%s",
        "/mnt/usb7/homebrew/%s",
        "/mnt/usb0/etaHEN/games/%s",
        "/mnt/usb1/etaHEN/games/%s",
        "/mnt/usb2/etaHEN/games/%s",
        "/mnt/usb3/etaHEN/games/%s",
        "/mnt/usb4/etaHEN/games/%s",
        "/mnt/usb5/etaHEN/games/%s",
        "/mnt/usb6/etaHEN/games/%s",
        "/mnt/usb7/etaHEN/games/%s",
        "/mnt/usb0/%s",
        "/mnt/usb1/%s",
        "/mnt/usb2/%s",
        "/mnt/usb3/%s",
        "/mnt/usb4/%s",
        "/mnt/usb5/%s",
        "/mnt/usb6/%s",
        "/mnt/usb7/%s",
        "/mnt/ext0/%s",
        "/mnt/ext1/%s"
    };
    char path[512];
    struct stat info;
    if(!valid_title_id(title_id)) return 0;
    for(size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        int written = snprintf(path, sizeof(path), formats[i], title_id);
        if(written > 0 && written < (int)sizeof(path)
           && stat(path, &info) == 0 && S_ISDIR(info.st_mode)) return 1;
    }
    return 0;
}

int
game_metadata_cache_icon(const char *title_id) {
    char source[MAX_ICON_PATH];
    char destination[512];
    if(!valid_title_id(title_id)
       || find_any_icon(title_id, source, sizeof(source)) != 0) return -1;
    mkdir(COVERS_DIR, 0755);
    if(snprintf(destination, sizeof(destination), "%s/%s.png",
                COVERS_DIR, title_id) >= (int)sizeof(destination)) return -1;
    return copy_file_atomic(source, destination);
}

int
game_metadata_find_icon(const char *title_id, char *output,
                        size_t output_size) {
    char source[MAX_ICON_PATH];
    if(!valid_title_id(title_id) || !output || output_size == 0) return -1;
    if(snprintf(output, output_size, "%s/%s.png", COVERS_DIR, title_id)
       < (int)output_size && regular_file(output)) return 0;
    if(find_any_icon(title_id, source, sizeof(source)) != 0) return -1;
    if(game_metadata_cache_icon(title_id) == 0
       && snprintf(output, output_size, "%s/%s.png", COVERS_DIR, title_id)
           < (int)output_size) return 0;
    return snprintf(output, output_size, "%s", source) < (int)output_size
        ? 0 : -1;
}
