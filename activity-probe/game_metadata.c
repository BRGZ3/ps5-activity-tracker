#include "game_metadata.h"
#include "tracker.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef USER_APPMETA_DIR
#define USER_APPMETA_DIR "/user/appmeta"
#endif
#ifndef SYSTEM_APPMETA_DIR
#define SYSTEM_APPMETA_DIR "/system_data/priv/appmeta"
#endif
#ifndef USER_APP_DIR
#define USER_APP_DIR "/user/app"
#endif
#ifndef SYSTEM_APP_DIR
#define SYSTEM_APP_DIR "/system_ex/app"
#endif

#define COVERS_DIR TRACKER_DATA_DIR "/covers"
#define MAX_ICON_SIZE (16 * 1024 * 1024)

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
find_live_icon(const char *title_id, char *output, size_t output_size) {
    static const char *formats[] = {
        USER_APPMETA_DIR "/%s/icon0.png",
        SYSTEM_APPMETA_DIR "/%s/icon0.png",
        USER_APP_DIR "/%s/icon0.png",
        USER_APP_DIR "/%s/sce_sys/icon0.png",
        SYSTEM_APP_DIR "/%s/sce_sys/icon0.png"
    };
    for(size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        int written = snprintf(output, output_size, formats[i], title_id);
        if(written > 0 && (size_t)written < output_size
           && regular_file(output)) return 0;
    }
    return -1;
}

static int
copy_file_atomic(const char *source, const char *destination) {
    char temporary[512];
    char buffer[8192];
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
    while((count = read(input, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;
        while(offset < count) {
            ssize_t written = write(
                output, buffer + offset, (size_t)(count - offset));
            if(written <= 0) {
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
        USER_APP_DIR "/%s",
        SYSTEM_APP_DIR "/%s"
    };
    char path[512];
    struct stat info;
    if(!valid_title_id(title_id)) return 0;
    for(size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        int written = snprintf(path, sizeof(path), formats[i], title_id);
        if(written > 0 && written < (int)sizeof(path)
           && stat(path, &info) == 0) return 1;
    }
    return 0;
}

int
game_metadata_cache_icon(const char *title_id) {
    char source[512];
    char destination[512];
    if(!valid_title_id(title_id)
       || find_live_icon(title_id, source, sizeof(source)) != 0) return -1;
    mkdir(COVERS_DIR, 0755);
    if(snprintf(destination, sizeof(destination), "%s/%s.png",
                COVERS_DIR, title_id) >= (int)sizeof(destination)) return -1;
    return copy_file_atomic(source, destination);
}

int
game_metadata_find_icon(const char *title_id, char *output,
                        size_t output_size) {
    if(!valid_title_id(title_id) || !output || output_size == 0) return -1;
    if(snprintf(output, output_size, "%s/%s.png", COVERS_DIR, title_id)
       < (int)output_size && regular_file(output)) return 0;
    if(find_live_icon(title_id, output, output_size) != 0) return -1;
    if(game_metadata_cache_icon(title_id) == 0
       && snprintf(output, output_size, "%s/%s.png", COVERS_DIR, title_id)
           < (int)output_size) return 0;
    return 0;
}
