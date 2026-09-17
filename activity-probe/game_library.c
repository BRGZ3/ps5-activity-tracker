#include "game_library.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

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

#define MAX_METADATA_SIZE (1024 * 1024)
#define APP_DB_BUSY_TIMEOUT_MS 5000
#define APP_DB_QUERY_RETRIES 3
#define APP_DB_RETRY_SLEEP_NANOSECONDS 200000000L

static char last_library_source[64] = "none";
static char last_database_status[64] = "not_attempted";
static unsigned last_database_rows;

static int
valid_title_id(const char *value) {
    if(!value || strlen(value) != 9
       || (strncmp(value, "CUSA", 4) != 0
           && strncmp(value, "PPSA", 4) != 0)) return 0;
    for(size_t i = 4; i < 9; i++) {
        if(value[i] < '0' || value[i] > '9') return 0;
    }
    return 1;
}

/* Normalize the ASCII title-ID shape while tolerating whitespace emitted by
 * image managers.  The database is normally TEXT, but filtering in C keeps
 * the scan working when a row has an unusual SQLite storage class. */
static int
normalize_title_id(const unsigned char *source,
                   char output[GAME_LIBRARY_TITLE_ID_SIZE]) {
    size_t start = 0;
    size_t length = 0;
    if(!source || !output) return 0;
    while(source[start] && isspace(source[start])) start++;
    while(source[start + length] && !isspace(source[start + length])) length++;
    if(length != GAME_LIBRARY_TITLE_ID_SIZE - 1) return 0;
    for(size_t i = 0; i < length; i++) {
        unsigned char value = source[start + i];
        output[i] = (char)(i < 4 ? toupper(value) : value);
    }
    output[length] = '\0';
    return valid_title_id(output);
}

static int
contains_ascii_case_insensitive(const unsigned char *text, const char *needle) {
    size_t needle_length;
    if(!text || !needle || !(needle_length = strlen(needle))) return 0;
    for(; *text; text++) {
        size_t offset = 0;
        while(offset < needle_length && text[offset]
              && tolower((unsigned char)text[offset])
                   == tolower((unsigned char)needle[offset])) {
            offset++;
        }
        if(offset == needle_length) return 1;
    }
    return 0;
}

/*
 * categoryType is not stable across firmware and image managers.  Filtering
 * by a numeric allow-list made valid games disappear on otherwise usable
 * app.db files.  Exclude only titles that are unambiguously known media/system
 * entries; every other CUSA/PPSA row is retained as a game candidate.
 */
static int
is_media_title(const char *title_id, const unsigned char *title_name,
               int category) {
    static const char *known_media_ids[] = {
        "PPSA01280", /* Share Factory Studio */
        "PPSA01650"  /* YouTube */
    };
    static const char *media_name_markers[] = {
        "youtube", "share factory", "netflix", "spotify", "twitch",
        "media player", "playstation store"
    };
    int media_category = category == 65536 || category == 65792
        || category == 66048 || category == 131328 || category == 131584
        || category == 16777216 || category == 33554432
        || category == 50331648 || category == 67108864
        || category == 100664320;
    for(size_t i = 0; i < sizeof(known_media_ids) / sizeof(known_media_ids[0]);
        i++) {
        if(title_id && strcmp(title_id, known_media_ids[i]) == 0) return 1;
    }
    if(!media_category || !title_name || !title_name[0]) return 0;
    for(size_t i = 0;
        i < sizeof(media_name_markers) / sizeof(media_name_markers[0]); i++) {
        if(contains_ascii_case_insensitive(title_name, media_name_markers[i])) {
            return 1;
        }
    }
    return 0;
}

static void
copy_text(char *destination, size_t destination_size, const unsigned char *source) {
    if(!destination || destination_size == 0) return;
    if(!source) {
        destination[0] = '\0';
        return;
    }
    snprintf(destination, destination_size, "%s", (const char *)source);
}

static int
read_file(const char *path, char **output, size_t *length) {
    struct stat info;
    FILE *file;
    char *buffer;
    size_t read_count;
    if(stat(path, &info) != 0 || info.st_size <= 0
       || info.st_size > MAX_METADATA_SIZE) return -1;
    file = fopen(path, "rb");
    if(!file) return -1;
    buffer = malloc((size_t)info.st_size + 1);
    if(!buffer) {
        fclose(file);
        return -1;
    }
    read_count = fread(buffer, 1, (size_t)info.st_size, file);
    fclose(file);
    if(read_count != (size_t)info.st_size) {
        free(buffer);
        return -1;
    }
    buffer[read_count] = '\0';
    *output = buffer;
    *length = read_count;
    return 0;
}

static int
json_value(const char *json, const char *key, char *output,
           size_t output_size) {
    char search[80];
    const char *cursor;
    size_t source_offset = 0;
    size_t written = 0;
    if(snprintf(search, sizeof(search), "\"%s\"", key)
       >= (int)sizeof(search)) return 0;
    cursor = strstr(json, search);
    if(!cursor || !(cursor = strchr(cursor + strlen(search), ':'))) return 0;
    while(*++cursor == ' ' || *cursor == '\t' || *cursor == '\r'
          || *cursor == '\n') {}
    if(*cursor++ != '"') return 0;
    while(cursor[source_offset] && cursor[source_offset] != '"'
          && written + 1 < output_size) {
        char value = cursor[source_offset++];
        if(value == '\\' && cursor[source_offset]) {
            value = cursor[source_offset++];
            if(value == 'n') value = '\n';
            else if(value == 'r') value = '\r';
            else if(value == 't') value = '\t';
        }
        output[written++] = value;
    }
    output[written] = '\0';
    return written > 0;
}

static int
json_metadata(const char *path, char *name, size_t name_size,
              char *version, size_t version_size) {
    char *json;
    size_t length;
    const char *english;
    int found_name = 0;
    int found_version = 0;
    if(read_file(path, &json, &length) != 0) return -1;
    (void)length;
    english = strstr(json, "\"en-US\"");
    if(english) found_name = json_value(english, "titleName", name, name_size);
    if(!found_name) found_name = json_value(json, "titleName", name, name_size);
    found_version = json_value(json, "contentVersion", version, version_size);
    if(!found_version) found_version = json_value(json, "version", version, version_size);
    free(json);
    return found_name || found_version ? 0 : -1;
}

static uint16_t
read_le16(const unsigned char *value) {
    return (uint16_t)value[0] | (uint16_t)value[1] << 8;
}

static uint32_t
read_le32(const unsigned char *value) {
    return (uint32_t)value[0] | (uint32_t)value[1] << 8
        | (uint32_t)value[2] << 16 | (uint32_t)value[3] << 24;
}

static int
sfo_metadata(const char *path, char *name, size_t name_size,
             char *version, size_t version_size) {
    char *sfo = NULL;
    size_t length;
    uint32_t key_table;
    uint32_t data_table;
    uint32_t entry_count;
    int found_name = 0;
    int found_version = 0;
    if(read_file(path, &sfo, &length) != 0 || length < 20
       || read_le32((unsigned char *)sfo) != 0x46535000u) {
        free(sfo);
        return -1;
    }
    key_table = read_le32((unsigned char *)sfo + 8);
    data_table = read_le32((unsigned char *)sfo + 12);
    entry_count = read_le32((unsigned char *)sfo + 16);
    if(key_table >= length || data_table >= length
       || entry_count > (length - 20) / 16) {
        free(sfo);
        return -1;
    }
    for(uint32_t i = 0; i < entry_count; i++) {
        const unsigned char *entry = (unsigned char *)sfo + 20 + i * 16;
        uint32_t key_offset = key_table + read_le16(entry);
        uint32_t value_length = read_le32(entry + 4);
        uint32_t value_offset = data_table + read_le32(entry + 12);
        const char *key;
        const char *value;
        size_t copy_length;
        if(key_offset >= length || value_offset >= length
           || value_length == 0 || value_length > length - value_offset) continue;
        key = sfo + key_offset;
        value = sfo + value_offset;
        if(!memchr(key, '\0', length - key_offset)) continue;
        copy_length = strnlen(value, value_length);
        if(!found_name && (strcmp(key, "TITLE") == 0
                           || strncmp(key, "TITLE_", 6) == 0)) {
            if(copy_length >= name_size) copy_length = name_size - 1;
            memcpy(name, value, copy_length);
            name[copy_length] = '\0';
            found_name = copy_length > 0;
        }
        if(!found_version && (strcmp(key, "VERSION") == 0
                              || strcmp(key, "APP_VER") == 0
                              || strcmp(key, "CONTENT_VERSION") == 0)) {
            if(copy_length >= version_size) copy_length = version_size - 1;
            memcpy(version, value, copy_length);
            version[copy_length] = '\0';
            found_version = copy_length > 0;
        }
    }
    free(sfo);
    return found_name || found_version ? 0 : -1;
}

static void
read_metadata(const char *directory, game_library_entry_t *entry) {
    static const char *json_names[] = {
        "/param.json", "/sce_sys/param.json"
    };
    static const char *sfo_names[] = {
        "/param.sfo", "/sce_sys/param.sfo"
    };
    char path[512];
    char parsed_name[GAME_LIBRARY_NAME_SIZE];
    char parsed_version[GAME_LIBRARY_VERSION_SIZE];
    for(size_t i = 0; i < sizeof(json_names) / sizeof(json_names[0]); i++) {
        parsed_name[0] = '\0';
        parsed_version[0] = '\0';
        if(snprintf(path, sizeof(path), "%s%s", directory, json_names[i])
           < (int)sizeof(path)
           && json_metadata(path, parsed_name, sizeof(parsed_name),
                            parsed_version, sizeof(parsed_version)) == 0) {
            if(!entry->name[0] && parsed_name[0]) {
                snprintf(entry->name, sizeof(entry->name), "%s", parsed_name);
            }
            if(!entry->version[0] && parsed_version[0]) {
                snprintf(entry->version, sizeof(entry->version), "%s",
                         parsed_version);
            }
            if(entry->name[0] && entry->version[0]) return;
        }
    }
    for(size_t i = 0; i < sizeof(sfo_names) / sizeof(sfo_names[0]); i++) {
        parsed_name[0] = '\0';
        parsed_version[0] = '\0';
        if(snprintf(path, sizeof(path), "%s%s", directory, sfo_names[i])
           < (int)sizeof(path)) {
            sfo_metadata(path, parsed_name, sizeof(parsed_name),
                         parsed_version, sizeof(parsed_version));
            if(!entry->name[0] && parsed_name[0]) {
                snprintf(entry->name, sizeof(entry->name), "%s", parsed_name);
            }
            if(!entry->version[0] && parsed_version[0]) {
                snprintf(entry->version, sizeof(entry->version), "%s",
                         parsed_version);
            }
            if(entry->name[0] && entry->version[0]) return;
        }
    }
}

static int
find_entry(game_library_entry_t *entries, size_t count, const char *title_id) {
    for(size_t i = 0; i < count; i++) {
        if(strcmp(entries[i].title_id, title_id) == 0) return (int)i;
    }
    return -1;
}

static int compare_entries(const void *left, const void *right);

static int
directory_exists(const char *path) {
    struct stat info;
    return path && path[0] && stat(path, &info) == 0
        && S_ISDIR(info.st_mode);
}

static int
path_has_title_component(const char *path, const char *title_id) {
    const char *cursor;
    size_t title_length;
    if(!path || !title_id || !path[0] || !title_id[0]) return 0;
    title_length = strlen(title_id);
    cursor = path;
    while((cursor = strstr(cursor, title_id)) != NULL) {
        char before = cursor == path ? '/' : cursor[-1];
        char after = cursor[title_length];
        if((before == '/' || before == '\\')
           && (after == '\0' || after == '/' || after == '\\')) return 1;
        cursor++;
    }
    return 0;
}

static int
path_is_metadata_cache(const char *path) {
    static const char *roots[] = {
        USER_APPMETA_DIR, SYSTEM_APPMETA_DIR, SYSTEM_MMS_APPMETA_DIR
    };
    if(!path || !path[0]) return 0;
    for(size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        size_t length = strlen(roots[i]);
        if(strncmp(path, roots[i], length) == 0
           && (path[length] == '\0' || path[length] == '/')) return 1;
    }
    return 0;
}

static int
regular_file_exists(const char *path) {
    struct stat info;
    return path && path[0] && stat(path, &info) == 0
        && S_ISREG(info.st_mode);
}

static int
source_directory_is_live(const char *path) {
    static const char *markers[] = {
        /* ShadowMount+ stages sce_sys/param.* under /user/app even after
         * the source image has been removed.  Metadata alone is therefore
         * deliberately not a liveness marker. */
        "eboot.bin", "app.pkg"
    };
    char candidate[GAME_LIBRARY_PATH_SIZE];
    if(!directory_exists(path)) return 0;
    for(size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
        if(snprintf(candidate, sizeof(candidate), "%s/%s", path, markers[i])
               < (int)sizeof(candidate)
           && regular_file_exists(candidate)) return 1;
    }
    return 0;
}

static int
mount_source_is_live(const char *path) {
    struct stat info;
    if(!path || !path[0] || stat(path, &info) != 0) return 0;
    if(S_ISDIR(info.st_mode)) return source_directory_is_live(path);
    if(!S_ISREG(info.st_mode)) return 0;
    {
        const char *extension = strrchr(path, '.');
        return extension
            && (strcasecmp(extension, ".exfat") == 0
                || strcasecmp(extension, ".ffpkg") == 0
                || strcasecmp(extension, ".ffpfs") == 0
                || strcasecmp(extension, ".ffpfsc") == 0
                || strcasecmp(extension, ".img") == 0);
    }
}

static int
mount_link_is_live(const char *path) {
    char target[GAME_LIBRARY_PATH_SIZE];
    FILE *file;
    size_t length;
    if(!regular_file_exists(path)) return 0;
    file = fopen(path, "rb");
    if(!file) return 0;
    if(!fgets(target, sizeof(target), file)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    length = strlen(target);
    while(length && (target[length - 1] == '\n'
                     || target[length - 1] == '\r'
                     || target[length - 1] == ' '
                     || target[length - 1] == '\t')) {
        target[--length] = '\0';
    }
    if(length >= 2 && target[0] == '"' && target[length - 1] == '"') {
        memmove(target, target + 1, length - 2);
        target[length - 2] = '\0';
    }
    return mount_source_is_live(target);
}

static int
content_root_is_live(const char *path) {
    char mount_link[GAME_LIBRARY_PATH_SIZE];
    if(!directory_exists(path)) return 0;
    if(snprintf(mount_link, sizeof(mount_link), "%s/mount.lnk", path)
           >= (int)sizeof(mount_link)) return 0;
    if(regular_file_exists(mount_link)) return mount_link_is_live(mount_link);
    return source_directory_is_live(path);
}

static int
metadata_path_is_installed(const game_library_entry_t *entry) {
    char nested[GAME_LIBRARY_PATH_SIZE];
    if(!entry || !entry->metadata_path[0]) return 0;
    /* appmeta is a shell metadata cache.  It can survive an uninstall, so
     * never use it as proof that the executable content is still present. */
    if(path_is_metadata_cache(entry->metadata_path)) return 0;
    /* app.db variants disagree on whether metaDataPath is the title folder
     * or the common appmeta root.  Never treat the common root itself as
     * evidence for every title; resolve it through the title ID instead. */
    if(path_has_title_component(entry->metadata_path, entry->title_id)) {
        if(content_root_is_live(entry->metadata_path)) return 1;
        {
            struct stat info;
            if(stat(entry->metadata_path, &info) == 0 && S_ISREG(info.st_mode)) {
                char parent[GAME_LIBRARY_PATH_SIZE];
                snprintf(parent, sizeof(parent), "%s", entry->metadata_path);
                char *separator = strrchr(parent, '/');
                if(separator) {
                    *separator = '\0';
                    if(content_root_is_live(parent)) return 1;
                }
            }
        }
    }
    if(!directory_exists(entry->metadata_path)
       || snprintf(nested, sizeof(nested), "%s/%s", entry->metadata_path,
                   entry->title_id) >= (int)sizeof(nested)) return 0;
    return content_root_is_live(nested);
}

static int
title_directory_exists(const char *title_id) {
    static const char *roots[] = {
        SYSTEM_APP_DIR, USER_APP_DIR,
        "/data/homebrew", "/data/etaHEN/games",
        "/mnt/ext0/homebrew", "/mnt/ext0/etaHEN/games",
        "/mnt/ext1/homebrew", "/mnt/ext1/etaHEN/games",
        "/mnt/usb0/homebrew", "/mnt/usb1/homebrew",
        "/mnt/usb2/homebrew", "/mnt/usb3/homebrew",
        "/mnt/usb4/homebrew", "/mnt/usb5/homebrew",
        "/mnt/usb6/homebrew", "/mnt/usb7/homebrew",
        "/mnt/usb0/etaHEN/games", "/mnt/usb1/etaHEN/games",
        "/mnt/usb2/etaHEN/games", "/mnt/usb3/etaHEN/games",
        "/mnt/usb4/etaHEN/games", "/mnt/usb5/etaHEN/games",
        "/mnt/usb6/etaHEN/games", "/mnt/usb7/etaHEN/games",
        "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3",
        "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7",
        "/mnt/ext0", "/mnt/ext1"
    };
    char path[GAME_LIBRARY_PATH_SIZE];
    if(!valid_title_id(title_id)) return 0;
    for(size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        if(snprintf(path, sizeof(path), "%s/%s", roots[i], title_id)
               < (int)sizeof(path)
           && content_root_is_live(path)) return 1;
    }
    return 0;
}

static void
database_retry_pause(void) {
    struct timespec pause = {0, APP_DB_RETRY_SLEEP_NANOSECONDS};
    nanosleep(&pause, NULL);
}

static int
open_app_database(sqlite3 **database) {
    int rc;
    if(!database) return SQLITE_MISUSE;
    *database = NULL;
    rc = sqlite3_open_v2(APP_DB_PATH, database, SQLITE_OPEN_READONLY, NULL);
    if(rc == SQLITE_OK) return rc;
    if(*database) {
        sqlite3_close(*database);
        *database = NULL;
    }

    /* A few PS5 database mounts reject the read-only open flag even though a
     * SELECT is permitted.  Retry with a read-write handle, but never issue
     * a mutating SQLite statement. */
    rc = sqlite3_open_v2(APP_DB_PATH, database, SQLITE_OPEN_READWRITE, NULL);
    if(rc != SQLITE_OK && *database) {
        sqlite3_close(*database);
        *database = NULL;
    }
    return rc;
}

/*
 * app.db is the console's title registry.  Portable/image-backed games may
 * not have a directory under /user/appmeta or /user/app, so those directories
 * cannot be the primary source for the library.  Keep this query read-only;
 * ShadowMountPlus and the shell both use tbl_contentinfo for title discovery.
 * categoryType values vary between firmware and image managers.  Known media
 * IDs/names are omitted, while unknown categories remain visible so a valid
 * game cannot disappear because its numeric category changed.
 */
static int
collect_from_app_database(game_library_entry_t *entries, size_t capacity,
                          size_t *count) {
    /*
     * categoryType and titleName are present in the stock 4.50 database, but
     * homebrew installers have shipped reduced schemas.  Try the complete
     * query first, then fall back to projections that only require titleId.
     * Every projection keeps the same four result columns so the row parser
     * remains independent of the schema variant.
     */
    static const char *queries[] = {
        "SELECT titleId, titleName, categoryType, metaDataPath "
        "FROM tbl_contentinfo "
        "WHERE titleId IS NOT NULL AND titleId != '' ORDER BY titleId;",
        "SELECT titleId, titleName, categoryType, NULL AS metaDataPath "
        "FROM tbl_contentinfo "
        "WHERE titleId IS NOT NULL AND titleId != '' ORDER BY titleId;",
        "SELECT titleId, titleName, 0 AS categoryType, NULL AS metaDataPath "
        "FROM tbl_contentinfo "
        "WHERE titleId IS NOT NULL AND titleId != '' ORDER BY titleId;",
        "SELECT titleId, NULL AS titleName, 0 AS categoryType, NULL AS metaDataPath "
        "FROM tbl_contentinfo "
        "WHERE titleId IS NOT NULL AND titleId != '' ORDER BY titleId;"
    };
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    size_t total = 0;
    unsigned long long raw_rows = 0;
    int selected_query = -1;
    int result = -1;
    int rc;

    if(!entries || !count) return -1;
    *count = 0;
    last_database_rows = 0;
    snprintf(last_database_status, sizeof(last_database_status), "opening");
    rc = open_app_database(&database);
    if(rc != SQLITE_OK) {
        snprintf(last_database_status, sizeof(last_database_status),
                 "open_rc_%d", rc);
        if(database) sqlite3_close(database);
        return -1;
    }
    (void)sqlite3_busy_timeout(database, APP_DB_BUSY_TIMEOUT_MS);
    for(size_t query_index = 0;
        query_index < sizeof(queries) / sizeof(queries[0]); query_index++) {
        statement = NULL;
        for(int attempt = 0; attempt < APP_DB_QUERY_RETRIES; attempt++) {
            rc = sqlite3_prepare_v2(database, queries[query_index], -1,
                                    &statement, NULL);
            if(rc == SQLITE_OK) break;
            if(statement) {
                sqlite3_finalize(statement);
                statement = NULL;
            }
            if((rc != SQLITE_BUSY && rc != SQLITE_LOCKED)
               || attempt + 1 >= APP_DB_QUERY_RETRIES) {
                break;
            }
            database_retry_pause();
        }
        if(rc == SQLITE_OK) {
            selected_query = (int)query_index;
            break;
        }
    }
    if(rc != SQLITE_OK || !statement) {
        snprintf(last_database_status, sizeof(last_database_status),
                 "prepare_rc_%d", rc);
        goto cleanup;
    }

    for(int busy_attempts = 0;;) {
        rc = sqlite3_step(statement);
        if(rc == SQLITE_DONE) {
            result = 0;
            break;
        }
        if(rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            if(++busy_attempts < APP_DB_QUERY_RETRIES) {
                database_retry_pause();
                continue;
            }
        }
        if(rc != SQLITE_ROW) break;

        const unsigned char *title_id = sqlite3_column_text(statement, 0);
        const unsigned char *title_name = sqlite3_column_text(statement, 1);
        const unsigned char *metadata_path = sqlite3_column_text(statement, 3);
        int category = sqlite3_column_int(statement, 2);
        char normalized_title_id[GAME_LIBRARY_TITLE_ID_SIZE];
        int index;
        if(raw_rows < ULLONG_MAX) raw_rows++;
        if(!normalize_title_id(title_id, normalized_title_id)) continue;
        if(is_media_title(normalized_title_id, title_name, category)) continue;
        index = find_entry(entries, total, normalized_title_id);
        if(index < 0) {
            if(total >= capacity) continue;
            index = (int)total++;
            memset(&entries[index], 0, sizeof(entries[index]));
            copy_text(entries[index].title_id, sizeof(entries[index].title_id),
                      (const unsigned char *)normalized_title_id);
        }
        if(!entries[index].name[0] && title_name && title_name[0]) {
            copy_text(entries[index].name, sizeof(entries[index].name),
                      title_name);
        }
        if(!entries[index].metadata_path[0] && metadata_path
           && metadata_path[0]) {
            copy_text(entries[index].metadata_path,
                      sizeof(entries[index].metadata_path), metadata_path);
            {
                char *query_marker = strchr(entries[index].metadata_path, '?');
                if(query_marker) *query_marker = '\0';
            }
        }
    }

cleanup:
    last_database_rows = raw_rows > UINT_MAX ? UINT_MAX : (unsigned)raw_rows;
    if(result == 0) {
        snprintf(last_database_status, sizeof(last_database_status),
                 selected_query == 0 ? "ok" : "ok_compat");
    } else if(rc != SQLITE_OK && rc != SQLITE_DONE) {
        snprintf(last_database_status, sizeof(last_database_status),
                 "step_rc_%d", rc);
    }
    if(statement) sqlite3_finalize(statement);
    sqlite3_close(database);
    if(result == 0) *count = total;
    return result;
}

static void
enrich_database_entries(game_library_entry_t *entries, size_t count) {
    static const char *roots[] = {
        USER_APPMETA_DIR, SYSTEM_APPMETA_DIR, SYSTEM_MMS_APPMETA_DIR,
        SYSTEM_APP_DIR, USER_APP_DIR,
        "/data/homebrew", "/data/etaHEN/games", "/data",
        "/mnt/ext0/homebrew", "/mnt/ext0/etaHEN/games",
        "/mnt/ext1/homebrew", "/mnt/ext1/etaHEN/games",
        "/mnt/usb0/homebrew", "/mnt/usb1/homebrew",
        "/mnt/usb2/homebrew", "/mnt/usb3/homebrew",
        "/mnt/usb4/homebrew", "/mnt/usb5/homebrew",
        "/mnt/usb6/homebrew", "/mnt/usb7/homebrew",
        "/mnt/usb0/etaHEN/games", "/mnt/usb1/etaHEN/games",
        "/mnt/usb2/etaHEN/games", "/mnt/usb3/etaHEN/games",
        "/mnt/usb4/etaHEN/games", "/mnt/usb5/etaHEN/games",
        "/mnt/usb6/etaHEN/games", "/mnt/usb7/etaHEN/games",
        "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3",
        "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7",
        "/mnt/ext0", "/mnt/ext1"
    };
    for(size_t i = 0; i < count; i++) {
        if(entries[i].metadata_path[0]) {
            read_metadata(entries[i].metadata_path, &entries[i]);
        }
        for(size_t root_index = 0;
            root_index < sizeof(roots) / sizeof(roots[0]); root_index++) {
            char path[512];
            struct stat info;
            if(snprintf(path, sizeof(path), "%s/%s", roots[root_index],
                        entries[i].title_id) >= (int)sizeof(path)
               || stat(path, &info) != 0 || !S_ISDIR(info.st_mode)) continue;
            read_metadata(path, &entries[i]);
            if(entries[i].name[0] && entries[i].version[0]) break;
        }
    }
}

static void
finish_entries(game_library_entry_t *entries, size_t total) {
    for(size_t i = 0; i < total; i++) {
        if(!entries[i].name[0]) {
            snprintf(entries[i].name, sizeof(entries[i].name), "%s",
                     entries[i].title_id);
        }
        if(!entries[i].version[0]) {
            snprintf(entries[i].version, sizeof(entries[i].version), "—");
        }
    }
    qsort(entries, total, sizeof(entries[0]), compare_entries);
}

static int
compare_entries(const void *left, const void *right) {
    const game_library_entry_t *first = left;
    const game_library_entry_t *second = right;
    return strcmp(first->title_id, second->title_id);
}

int
game_library_collect(game_library_entry_t *entries, size_t capacity,
                     size_t *count) {
    static const char *roots[] = {
        USER_APPMETA_DIR, SYSTEM_APPMETA_DIR, SYSTEM_MMS_APPMETA_DIR,
        SYSTEM_APP_DIR, USER_APP_DIR,
        "/data/homebrew", "/data/etaHEN/games", "/data",
        "/mnt/ext0/homebrew", "/mnt/ext0/etaHEN/games",
        "/mnt/ext1/homebrew", "/mnt/ext1/etaHEN/games",
        "/mnt/usb0/homebrew", "/mnt/usb1/homebrew",
        "/mnt/usb2/homebrew", "/mnt/usb3/homebrew",
        "/mnt/usb4/homebrew", "/mnt/usb5/homebrew",
        "/mnt/usb6/homebrew", "/mnt/usb7/homebrew",
        "/mnt/usb0/etaHEN/games", "/mnt/usb1/etaHEN/games",
        "/mnt/usb2/etaHEN/games", "/mnt/usb3/etaHEN/games",
        "/mnt/usb4/etaHEN/games", "/mnt/usb5/etaHEN/games",
        "/mnt/usb6/etaHEN/games", "/mnt/usb7/etaHEN/games",
        "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3",
        "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7",
        "/mnt/ext0", "/mnt/ext1"
    };
    size_t total = 0;
    size_t database_total = 0;
    int database_result;
    if(!entries || !count) return -1;
    snprintf(last_library_source, sizeof(last_library_source), "none");

    /* Prefer the title registry so portable PS5 images are visible too. */
    database_result = collect_from_app_database(entries, capacity, &total);
    database_total = total;

    /*
     * Older/test environments may not expose app.db.  Also keep the legacy
     * directory scan when the registry is temporarily empty (for example
     * while mms is rebuilding it after an image mount).
     */
    for(size_t root_index = 0; root_index < sizeof(roots) / sizeof(roots[0]);
        root_index++) {
        DIR *directory = opendir(roots[root_index]);
        struct dirent *item;
        if(!directory) continue;
        while((item = readdir(directory)) != NULL) {
            char path[512];
            struct stat info;
            int index;
            if(item->d_name[0] == '.' || !valid_title_id(item->d_name)) continue;
            if(snprintf(path, sizeof(path), "%s/%s", roots[root_index],
                        item->d_name) >= (int)sizeof(path)
               || stat(path, &info) != 0 || !S_ISDIR(info.st_mode)) continue;
            index = find_entry(entries, total, item->d_name);
            if(index < 0) {
                if(total >= capacity) continue;
                index = (int)total++;
                memset(&entries[index], 0, sizeof(entries[index]));
                snprintf(entries[index].title_id, sizeof(entries[index].title_id),
                         "%s", item->d_name);
            }
            read_metadata(path, &entries[index]);
        }
        closedir(directory);
    }
    /* A successful app.db query can still omit an image that was mounted
     * after mms last refreshed its registry.  Merge directory discoveries
     * instead of returning early so both sources are represented. */
    enrich_database_entries(entries, total);
    finish_entries(entries, total);
    if(total > 0) {
        snprintf(last_library_source, sizeof(last_library_source),
                 database_result == 0
                     ? (database_total > 0 && total > database_total
                            ? "app.db+filesystem"
                            : (database_total > 0 ? "app.db" :
                               "filesystem_after_empty_db"))
                     : "filesystem_after_db_error");
    } else {
        snprintf(last_library_source, sizeof(last_library_source),
                 database_result == 0 ? "empty" : "empty_after_db_error");
    }
    *count = total;
    return 0;
}

int
game_library_entry_is_installed(const game_library_entry_t *entry) {
    if(!entry || !valid_title_id(entry->title_id)) return 0;
    if(metadata_path_is_installed(entry)) return 1;
    return title_directory_exists(entry->title_id);
}

const char *
game_library_source(void) {
    return last_library_source;
}

const char *
game_library_database_status(void) {
    return last_database_status;
}

unsigned
game_library_database_rows(void) {
    return last_database_rows;
}
