#include "offline_update.h"
#include "tracker.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The small host-side autoload harness links this file without tracker.c.
   The production runtime overrides this weak fallback with the real backup. */
__attribute__((weak)) int
tracker_backup_create(uint64_t now_ms, char *backup_id,
                      unsigned backup_id_size) {
    (void)now_ms;
    if(backup_id && backup_id_size) backup_id[0] = '\0';
    return -1;
}

#define UPDATE_CARRIER_COMPAT "/user/appmeta/ACTV00006/icon0.png"
#define UPDATE_CARRIER_CLEAN "/user/appmeta/ACTV00005/icon0.png"
#define UPDATE_CARRIER_PRIMARY "/user/appmeta/ACTV00003/icon0.png"
#define UPDATE_CARRIER_RELEASE "/user/appmeta/ACTV00002/icon0.png"
#define UPDATE_STATE "/data/ps5-activity/applied-update.txt"
#define INSTALL_STATE "/data/ps5-activity/install.json"
#define RUNTIME_DIR "/data/ps5-activity/runtime"
#define RUNTIME_TARGET RUNTIME_DIR "/Playlog.elf"
#define DASHBOARD_TARGET "/data/ps5-activity/dashboard/index.html"
#define ETAHEN_TARGET "/data/etaHEN/plugins/ps5-activity-tracker.plugin"
#ifdef LITE_INSTALLER
#define ETAHEN_ELF_TARGET \
    "/data/etaHEN/plugins/playlog-compatibility-test.elf"
#define AUTOLOADER_TARGET \
    "/data/ps5_autoloader/playlog-compatibility-test.elf"
#define AUTOLOADER_NAME "playlog-compatibility-test.elf"
#else
#define ETAHEN_ELF_TARGET "/data/etaHEN/plugins/Playlog.elf"
#define AUTOLOADER_TARGET "/data/ps5_autoloader/Playlog.elf"
#define AUTOLOADER_NAME "Playlog.elf"
#endif
#define ETAHEN_ELF_AUTOSTART ETAHEN_ELF_TARGET ".auto_start"
#define AUTOLOADER_LIST "/data/ps5_autoloader/autoload.txt"
#define MAX_BUNDLE_SIZE (8u * 1024u * 1024u)
#define BUNDLE_VERSION 2u

#ifndef TRACKER_VERSION
#define TRACKER_VERSION "dev"
#endif
#ifndef DASHBOARD_VERSION
#define DASHBOARD_VERSION "dev"
#endif

enum update_entry_type {
    ENTRY_DASHBOARD = 1,
    ENTRY_PLUGIN = 2,
    ENTRY_ELF = 3
};

typedef struct __attribute__((packed)) bundle_header {
    char magic[8];
    uint32_t version;
    uint32_t entry_count;
    char package_version[16];
    char tracker_version[16];
    char dashboard_version[16];
} bundle_header_t;

typedef struct __attribute__((packed)) entry_header {
    uint32_t type;
    uint32_t length;
    uint32_t crc32;
    uint32_t reserved;
} entry_header_t;

typedef struct __attribute__((packed)) bundle_footer {
    char magic[8];
    uint64_t offset;
    uint64_t length;
    uint32_t crc32;
    unsigned char reserved[28];
} bundle_footer_t;

typedef struct update_bundle {
    const char *path;
    uint64_t offset;
    uint64_t length;
    bundle_header_t header;
} update_bundle_t;

static uint32_t
crc32_update(uint32_t crc, const unsigned char *data, size_t size) {
    crc = ~crc;
    while(size--) {
        crc ^= *data++;
        for(int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int)(crc & 1));
        }
    }
    return ~crc;
}

static int
path_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0;
}

static int
read_applied_version(char output[16]) {
    FILE *file = fopen(UPDATE_STATE, "rb");
    size_t length;
    output[0] = '\0';
    if(!file) return -1;
    length = fread(output, 1, 15, file);
    fclose(file);
    while(length && (output[length - 1] == '\n'
                     || output[length - 1] == '\r')) {
        length--;
    }
    output[length] = '\0';
    return length ? 0 : -1;
}

static int
write_applied_version(const char *version) {
    char temporary[256];
    FILE *file;
    int failed;
    snprintf(temporary, sizeof(temporary), "%s.new", UPDATE_STATE);
    file = fopen(temporary, "wb");
    if(!file) return -1;
    failed = fprintf(file, "%s\n", version) < 0 || fflush(file) != 0
        || fsync(fileno(file)) != 0;
    if(fclose(file) != 0) failed = 1;
    if(failed) {
        unlink(temporary);
        return -1;
    }
    if(rename(temporary, UPDATE_STATE) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}

static int
read_bundle(const char *path, update_bundle_t *bundle) {
    struct stat info;
    bundle_footer_t footer;
    FILE *file;
    uint32_t crc = 0;
    unsigned char buffer[8192];
    uint64_t remaining;
    if(stat(path, &info) != 0
       || info.st_size < (off_t)(sizeof(footer) + sizeof(bundle_header_t))) {
        return -1;
    }
    file = fopen(path, "rb");
    if(!file) return -1;
    if(fseeko(file, info.st_size - (off_t)sizeof(footer), SEEK_SET) != 0
       || fread(&footer, sizeof(footer), 1, file) != 1
       || memcmp(footer.magic, "PLGUPD02", 8) != 0
       || footer.length < sizeof(bundle_header_t)
       || footer.length > MAX_BUNDLE_SIZE
       || footer.offset > (uint64_t)info.st_size
       || footer.length > (uint64_t)info.st_size - footer.offset
       || footer.offset + footer.length + sizeof(footer)
           != (uint64_t)info.st_size
       || fseeko(file, (off_t)footer.offset, SEEK_SET) != 0
       || fread(&bundle->header, sizeof(bundle->header), 1, file) != 1
       || memcmp(bundle->header.magic, "PLGBND02", 8) != 0
       || bundle->header.version != BUNDLE_VERSION
       || bundle->header.entry_count == 0
       || bundle->header.entry_count > 8) {
        fclose(file);
        return -1;
    }
    if(fseeko(file, (off_t)footer.offset, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    remaining = footer.length;
    while(remaining) {
        size_t wanted = remaining > sizeof(buffer)
            ? sizeof(buffer) : (size_t)remaining;
        size_t count = fread(buffer, 1, wanted, file);
        if(count != wanted) {
            fclose(file);
            return -1;
        }
        crc = crc32_update(crc, buffer, count);
        remaining -= count;
    }
    fclose(file);
    if(crc != footer.crc32) return -1;
    bundle->path = path;
    bundle->offset = footer.offset;
    bundle->length = footer.length;
    bundle->header.package_version[15] = '\0';
    bundle->header.tracker_version[15] = '\0';
    bundle->header.dashboard_version[15] = '\0';
    return 0;
}

static int
find_bundle(update_bundle_t *bundle) {
    if(read_bundle(UPDATE_CARRIER_RELEASE, bundle) == 0) return 0;
    if(read_bundle(UPDATE_CARRIER_CLEAN, bundle) == 0) return 0;
    if(read_bundle(UPDATE_CARRIER_COMPAT, bundle) == 0) return 0;
    if(read_bundle(UPDATE_CARRIER_PRIMARY, bundle) == 0) return 0;
    return -1;
}

static const char *
installed_runtime_target(void) {
    char state[512];
    FILE *file = fopen(INSTALL_STATE, "rb");
    size_t length = 0;
    if(file) {
        length = fread(state, 1, sizeof(state) - 1, file);
        fclose(file);
        state[length] = '\0';
        if(strstr(state, "\"install_mode\": \"etahen\"")
           || strstr(state, "\"install_mode\":\"etahen\"")) {
            return ETAHEN_ELF_TARGET;
        }
        if(strstr(state, "\"install_mode\": \"autoloader\"")
           || strstr(state, "\"install_mode\":\"autoloader\"")) {
            return AUTOLOADER_TARGET;
        }
    }
    if(path_exists(ETAHEN_ELF_TARGET)) return ETAHEN_ELF_TARGET;
    return AUTOLOADER_TARGET;
}

static const char *
target_for_type(uint32_t type) {
    if(type == ENTRY_DASHBOARD) return DASHBOARD_TARGET;
    if(type == ENTRY_PLUGIN) return ETAHEN_TARGET;
    if(type == ENTRY_ELF) return installed_runtime_target();
    return NULL;
}

static int
copy_entry(FILE *source, const entry_header_t *entry, const char *target) {
    char temporary[320];
    unsigned char buffer[8192];
    uint32_t crc = 0;
    uint32_t remaining = entry->length;
    int output;
    snprintf(temporary, sizeof(temporary), "%s.new", target);
    output = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if(output < 0) return -1;
    while(remaining) {
        size_t wanted = remaining > sizeof(buffer)
            ? sizeof(buffer) : remaining;
        size_t count = fread(buffer, 1, wanted, source);
        size_t offset = 0;
        if(count != wanted) goto failure;
        crc = crc32_update(crc, buffer, count);
        while(offset < count) {
            ssize_t written = write(output, buffer + offset, count - offset);
            if(written <= 0) goto failure;
            offset += (size_t)written;
        }
        remaining -= (uint32_t)count;
    }
    if(crc != entry->crc32 || fsync(output) != 0 || close(output) != 0) {
        output = -1;
        goto failure;
    }
    output = -1;
    if(rename(temporary, target) != 0) goto failure;
    return 0;
failure:
    if(output >= 0) close(output);
    unlink(temporary);
    return -1;
}

static int
copy_file_atomic(const char *source_path, const char *target) {
    FILE *source = fopen(source_path, "rb");
    char temporary[320];
    unsigned char buffer[8192];
    int output;
    size_t count;
    if(!source) return -1;
    snprintf(temporary, sizeof(temporary), "%s.new", target);
    output = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if(output < 0) {
        fclose(source);
        return -1;
    }
    while((count = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        size_t offset = 0;
        while(offset < count) {
            ssize_t written = write(output, buffer + offset, count - offset);
            if(written <= 0) goto failure;
            offset += (size_t)written;
        }
    }
    if(ferror(source) || fsync(output) != 0) goto failure;
    fclose(source);
    if(close(output) != 0 || rename(temporary, target) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
failure:
    fclose(source);
    close(output);
    unlink(temporary);
    return -1;
}

int
offline_ensure_autoload_entry(const char *list_path,
                              const char *entry_name) {
    char temporary[320];
    struct stat info;
    FILE *input = NULL;
    FILE *output = NULL;
    char *contents = NULL;
    size_t length = 0;
    size_t position = 0;
    int has_other_payload = 0;
    int has_target = 0;
    int failed = 0;

    if(!list_path || !entry_name || !entry_name[0]
       || strchr(entry_name, '\n') || strchr(entry_name, '\r')) return -1;
    if(snprintf(temporary, sizeof(temporary), "%s.new", list_path)
       >= (int)sizeof(temporary)) return -1;
    if(stat(list_path, &info) != 0) {
        return errno == ENOENT ? OFFLINE_AUTOLOAD_MANUAL_REQUIRED : -1;
    }
    if(info.st_size < 0 || info.st_size > 64 * 1024) return -1;
    input = fopen(list_path, "rb");
    if(!input) return -1;
    length = (size_t)info.st_size;
    contents = malloc(length + 1);
    if(!contents) {
        fclose(input);
        return -1;
    }
    if(length && fread(contents, 1, length, input) != length) {
        fclose(input);
        free(contents);
        return -1;
    }
    fclose(input);
    contents[length] = '\0';

    while(position < length) {
        size_t start = position;
        size_t line_length;
        while(position < length && contents[position] != '\n') position++;
        line_length = position - start;
        if(line_length && contents[start + line_length - 1] == '\r') {
            line_length--;
        }
        for(size_t i = 0; i < line_length; i++) {
            if(contents[start + i] == '#') {
                line_length = i;
                break;
            }
        }
        while(line_length
              && (contents[start + line_length - 1] == ' '
                  || contents[start + line_length - 1] == '\t')) {
            line_length--;
        }
        if(line_length && contents[start] != '!' && contents[start] != '@') {
            if(line_length == strlen(entry_name)
               && memcmp(contents + start, entry_name, line_length) == 0) {
                has_target = 1;
            } else {
                has_other_payload = 1;
            }
        }
        if(position < length) position++;
    }
    if(has_target) {
        free(contents);
        return has_other_payload ? OFFLINE_AUTOLOAD_CONFIGURED
                                 : OFFLINE_AUTOLOAD_MANUAL_REQUIRED;
    }
    if(!has_other_payload) {
        free(contents);
        return OFFLINE_AUTOLOAD_MANUAL_REQUIRED;
    }

    output = fopen(temporary, "wb");
    if(!output) {
        free(contents);
        return -1;
    }
    if(length && fwrite(contents, 1, length, output) != length) failed = 1;
    if(!failed && length && contents[length - 1] != '\n'
       && fputc('\n', output) == EOF) failed = 1;
    if(!failed && (fputs("!5000\n", output) == EOF
                   || fputs(entry_name, output) == EOF
                   || fputc('\n', output) == EOF)) {
        failed = 1;
    }
    if(!failed && (fflush(output) != 0 || fsync(fileno(output)) != 0)) {
        failed = 1;
    }
    if(fclose(output) != 0) failed = 1;
    free(contents);
    if(failed || rename(temporary, list_path) != 0) {
        unlink(temporary);
        return -1;
    }
    return OFFLINE_AUTOLOAD_CONFIGURED;
}

int
offline_remove_autoload_entry(const char *list_path,
                              const char *entry_name) {
    char temporary[320];
    struct stat info;
    FILE *input = NULL;
    FILE *output = NULL;
    char *contents = NULL;
    size_t length = 0;
    size_t position = 0;
    int entry_found = 0;
    int failed = 0;

    if(!list_path || !entry_name || !entry_name[0]
       || strchr(entry_name, '\n') || strchr(entry_name, '\r')) return -1;
    if(stat(list_path, &info) != 0) return 0;
    if(info.st_size < 0 || info.st_size > 64 * 1024
       || snprintf(temporary, sizeof(temporary), "%s.new", list_path)
           >= (int)sizeof(temporary)) return -1;
    input = fopen(list_path, "rb");
    if(!input) return -1;
    length = (size_t)info.st_size;
    contents = malloc(length + 1);
    if(!contents || (length && fread(contents, 1, length, input) != length)) {
        fclose(input);
        free(contents);
        return -1;
    }
    fclose(input);
    contents[length] = '\0';
    while(position < length) {
        size_t start = position;
        size_t line_length;
        while(position < length && contents[position] != '\n') position++;
        line_length = position - start;
        if(line_length && contents[start + line_length - 1] == '\r') {
            line_length--;
        }
        if(line_length == strlen(entry_name)
           && memcmp(contents + start, entry_name, line_length) == 0) {
            entry_found = 1;
            break;
        }
        if(position < length) position++;
    }
    if(!entry_found) {
        free(contents);
        return 0;
    }
    position = 0;
    output = fopen(temporary, "wb");
    if(!output) {
        free(contents);
        return -1;
    }
    while(position < length) {
        size_t start = position;
        size_t line_length;
        while(position < length && contents[position] != '\n') position++;
        line_length = position - start;
        if(line_length && contents[start + line_length - 1] == '\r') {
            line_length--;
        }
        if(line_length != strlen(entry_name)
           || memcmp(contents + start, entry_name, line_length) != 0) {
            size_t raw_length = position - start;
            if(position < length) raw_length++;
            if(raw_length
               && fwrite(contents + start, 1, raw_length, output)
                    != raw_length) {
                failed = 1;
                break;
            }
        }
        if(position < length) position++;
    }
    if(!failed && (fflush(output) != 0 || fsync(fileno(output)) != 0)) {
        failed = 1;
    }
    if(fclose(output) != 0) failed = 1;
    free(contents);
    if(failed || rename(temporary, list_path) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}

static int
ensure_autoload_line(void) {
    return offline_ensure_autoload_entry(AUTOLOADER_LIST, AUTOLOADER_NAME);
}

static int
write_install_state(const char *mode, const update_bundle_t *bundle) {
    char temporary[256];
    FILE *file;
    int failed;
    snprintf(temporary, sizeof(temporary), "%s.new", INSTALL_STATE);
    file = fopen(temporary, "wb");
    if(!file) return -1;
    failed = fprintf(
        file,
        "{\n  \"schema\": 1,\n  \"install_mode\": \"%s\",\n"
        "  \"tracker_version\": \"%s\",\n"
        "  \"dashboard_version\": \"%s\",\n  \"configured\": true\n}\n",
        mode, bundle->header.tracker_version,
        bundle->header.dashboard_version) < 0
        || fflush(file) != 0 || fsync(fileno(file)) != 0;
    if(fclose(file) != 0) failed = 1;
    if(failed || rename(temporary, INSTALL_STATE) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}

int
offline_setup_status_json(char *output, size_t output_size) {
    update_bundle_t bundle;
    int configured = path_exists(INSTALL_STATE);
    if(find_bundle(&bundle) != 0) {
        return snprintf(
            output, output_size,
            "{\"ok\":false,\"configured\":%s,"
            "\"error\":\"Playlog PKG not found\"}\n",
            configured ? "true" : "false") < (int)output_size ? 0 : -1;
    }
    return snprintf(
        output, output_size,
        "{\"ok\":true,\"configured\":%s,\"package_version\":\"%s\","
        "\"tracker_version\":\"%s\",\"dashboard_version\":\"%s\"}\n",
        configured ? "true" : "false", bundle.header.package_version,
        bundle.header.tracker_version, bundle.header.dashboard_version)
        < (int)output_size ? 0 : -1;
}

int
offline_setup_install(const char *mode, char *output, size_t output_size) {
    update_bundle_t bundle;
    FILE *source = NULL;
    uint64_t consumed = sizeof(bundle_header_t);
    int dashboard_written = 0;
    int runtime_written = 0;
    int autoload_result = 0;
    if(strcmp(mode, "etahen") != 0 && strcmp(mode, "autoloader") != 0) {
        goto invalid;
    }
    if(find_bundle(&bundle) != 0) goto invalid;
    mkdir("/data/ps5-activity/dashboard", 0755);
    mkdir(RUNTIME_DIR, 0755);
    if(strcmp(mode, "etahen") == 0) {
        mkdir("/data/etaHEN", 0755);
        mkdir("/data/etaHEN/plugins", 0755);
    } else {
        mkdir("/data/ps5_autoloader", 0755);
    }
    source = fopen(bundle.path, "rb");
    if(!source
       || fseeko(source, (off_t)(bundle.offset + sizeof(bundle_header_t)),
                 SEEK_SET) != 0) goto invalid;
    for(uint32_t i = 0; i < bundle.header.entry_count; i++) {
        entry_header_t entry;
        if(consumed + sizeof(entry) > bundle.length
           || fread(&entry, sizeof(entry), 1, source) != 1) goto invalid;
        consumed += sizeof(entry);
        if(entry.length == 0 || consumed + entry.length > bundle.length) {
            goto invalid;
        }
        if(entry.type == ENTRY_DASHBOARD) {
            if(copy_entry(source, &entry, DASHBOARD_TARGET) != 0) goto failure;
            dashboard_written = 1;
        } else if(entry.type == ENTRY_ELF) {
            if(copy_entry(source, &entry, RUNTIME_TARGET) != 0) goto failure;
            runtime_written = 1;
        } else {
            if(fseeko(source, entry.length, SEEK_CUR) != 0) goto invalid;
        }
        consumed += entry.length;
    }
    fclose(source);
    source = NULL;
    if(consumed != bundle.length || !dashboard_written || !runtime_written) {
        goto invalid;
    }
    if(strcmp(mode, "etahen") == 0) {
        unlink(ETAHEN_TARGET);
        unlink(ETAHEN_TARGET ".auto_start");
        unlink("/data/etaHEN/plugins/ps5-activity-tracker.elf");
        unlink("/data/etaHEN/plugins/ps5-activity-tracker.elf.auto_start");
        int marker;
        if(copy_file_atomic(RUNTIME_TARGET, ETAHEN_ELF_TARGET) != 0) {
            goto failure;
        }
        marker = open(ETAHEN_ELF_AUTOSTART, O_WRONLY | O_CREAT, 0755);
        if(marker < 0) goto failure;
        close(marker);
    } else {
        unlink("/data/ps5_autoloader/ps5-activity-tracker.elf");
        if(offline_remove_autoload_entry(
                AUTOLOADER_LIST, "ps5-activity-tracker.elf") != 0) {
            goto failure;
        }
        if(copy_file_atomic(RUNTIME_TARGET, AUTOLOADER_TARGET) != 0) {
            goto failure;
        }
        autoload_result = ensure_autoload_line();
        if(autoload_result < 0) goto failure;
    }
    if(write_install_state(mode, &bundle) != 0
       || write_applied_version(bundle.header.package_version) != 0) {
        goto failure;
    }
    return snprintf(
        output, output_size,
        "{\"ok\":true,\"mode\":\"%s\",\"tracker_version\":\"%s\","
        "\"restart_required\":true,\"manual_autoload_required\":%s}\n",
        mode, bundle.header.tracker_version,
        autoload_result == OFFLINE_AUTOLOAD_MANUAL_REQUIRED ? "true" : "false")
        < (int)output_size ? 0 : -1;
invalid:
    if(source) fclose(source);
    snprintf(output, output_size,
             "{\"ok\":false,\"error\":\"invalid clean-install package\"}\n");
    return -1;
failure:
    if(source) fclose(source);
    snprintf(output, output_size,
             "{\"ok\":false,\"error\":\"could not install Playlog files\"}\n");
    return -1;
}

int
offline_update_status_json(char *output, size_t output_size) {
    update_bundle_t bundle;
    char applied[16] = "";
    int available;
    int pending_restart;
    if(find_bundle(&bundle) != 0) {
        return snprintf(
            output, output_size,
            "{\"ok\":true,\"available\":false,"
            "\"installed_tracker\":\"%s\","
            "\"installed_dashboard\":\"%s\"}\n",
            TRACKER_VERSION, DASHBOARD_VERSION) < (int)output_size ? 0 : -1;
    }
    read_applied_version(applied);
    available = strcmp(applied, bundle.header.package_version) != 0;
    pending_restart = !available
        && strcmp(TRACKER_VERSION, bundle.header.tracker_version) != 0;
    return snprintf(
        output, output_size,
        "{\"ok\":true,\"available\":%s,\"pending_restart\":%s,"
        "\"package_version\":\"%s\","
        "\"tracker_version\":\"%s\",\"dashboard_version\":\"%s\","
        "\"installed_tracker\":\"%s\",\"installed_dashboard\":\"%s\","
        "\"carrier\":\"%s\"}\n",
        available ? "true" : "false",
        pending_restart ? "true" : "false",
        bundle.header.package_version,
        bundle.header.tracker_version, bundle.header.dashboard_version,
        TRACKER_VERSION, DASHBOARD_VERSION, bundle.path)
        < (int)output_size ? 0 : -1;
}

int
offline_update_apply(char *output, size_t output_size) {
    update_bundle_t bundle;
    FILE *source = NULL;
    char backup_id[64] = "";
    struct timespec now;
    uint64_t consumed = sizeof(bundle_header_t);
    int dashboard = 0;
    int runtime = 0;
    int autoload_result = 0;
    if(find_bundle(&bundle) != 0) goto invalid;
    if(!tracker_backup_create || clock_gettime(CLOCK_REALTIME, &now) != 0
       || tracker_backup_create(
              (uint64_t)now.tv_sec * 1000
                  + (uint64_t)now.tv_nsec / 1000000,
              backup_id, sizeof(backup_id)) != 0) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"automatic backup failed; update was not applied\"}\n");
        return -1;
    }
    source = fopen(bundle.path, "rb");
    if(!source
       || fseeko(source, (off_t)(bundle.offset + sizeof(bundle_header_t)),
                 SEEK_SET) != 0) goto invalid;
    for(uint32_t i = 0; i < bundle.header.entry_count; i++) {
        entry_header_t entry;
        const char *target;
        if(consumed + sizeof(entry) > bundle.length
           || fread(&entry, sizeof(entry), 1, source) != 1) goto invalid;
        consumed += sizeof(entry);
        if(entry.length == 0 || consumed + entry.length > bundle.length) {
            goto invalid;
        }
        target = target_for_type(entry.type);
        if(!target) goto invalid;
        if(entry.type == ENTRY_PLUGIN && !path_exists(ETAHEN_TARGET)) {
            if(fseeko(source, entry.length, SEEK_CUR) != 0) goto invalid;
        } else if(entry.type == ENTRY_ELF && !path_exists(target)) {
            if(fseeko(source, entry.length, SEEK_CUR) != 0) goto invalid;
        } else {
            if(copy_entry(source, &entry, target) != 0) goto failure;
            if(entry.type == ENTRY_DASHBOARD) dashboard = 1;
            else runtime = 1;
        }
        consumed += entry.length;
    }
    fclose(source);
    source = NULL;
    if(runtime && strcmp(installed_runtime_target(), AUTOLOADER_TARGET) == 0) {
        autoload_result = ensure_autoload_line();
        if(autoload_result < 0) goto failure;
    }
    if(consumed != bundle.length
       || write_applied_version(bundle.header.package_version) != 0) {
        goto failure;
    }
    return snprintf(
        output, output_size,
        "{\"ok\":true,\"package_version\":\"%s\","
        "\"dashboard_updated\":%s,\"runtime_updated\":%s,"
        "\"restart_required\":%s,\"manual_autoload_required\":%s,"
        "\"backup_id\":\"%s\"}\n",
        bundle.header.package_version, dashboard ? "true" : "false",
        runtime ? "true" : "false", runtime ? "true" : "false",
        autoload_result == OFFLINE_AUTOLOAD_MANUAL_REQUIRED ? "true" : "false",
        backup_id)
        < (int)output_size ? 0 : -1;
invalid:
    if(source) fclose(source);
    snprintf(output, output_size,
             "{\"ok\":false,\"error\":\"invalid update package\"}\n");
    return -1;
failure:
    if(source) fclose(source);
    snprintf(output, output_size,
             "{\"ok\":false,\"error\":\"could not write update files\"}\n");
    return -1;
}
