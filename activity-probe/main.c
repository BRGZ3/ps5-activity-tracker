#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "tracker.h"
#include "http_server.h"
#include "app_focus_monitor.h"
#include "power_monitor.h"

#define DATA_DIR "/data/ps5-activity"
#define EVENT_PATH DATA_DIR "/probe-events.jsonl"
#define PID_PATH DATA_DIR "/probe.pid"
#define STOP_PATH DATA_DIR "/stop"
#define KLOG_PATH "/dev/klog"

#define LINE_CAPACITY 8192
#define TITLE_ID_LENGTH 9
#define MAX_APP_MAPPINGS 32
#define MAX_TITLE_NAME 256
#define MAX_PARAM_JSON_SIZE (1024 * 1024)
#define ENABLE_DIAGNOSTIC_CAPTURE 0
#define STARTUP_DELAY_SECONDS 5
#define IDLE_POLL_NANOSECONDS 100000000L

#ifndef PROBE_VERSION
#define PROBE_VERSION "dev"
#endif

typedef struct notify_request {
    char reserved[45];
    char message[3075];
} notify_request_t;

typedef struct app_mapping {
    uint32_t app_id;
    char title_id[TITLE_ID_LENGTH + 1];
} app_mapping_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static FILE *event_file;
static app_mapping_t mappings[MAX_APP_MAPPINGS];
static size_t mapping_count;
static char pending_title[TITLE_ID_LENGTH + 1];
static char foreground_title[TITLE_ID_LENGTH + 1];
static uint32_t foreground_app_id;
static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t received_signal;
static int instance_fd = -1;
static char stop_reason[128] = "runtime stop";

static void
handle_signal(int signal_number) {
    received_signal = signal_number;
    stop_requested = 1;
}

static int
notify(const char *format, ...) {
    notify_request_t request = {0};
    va_list args;

    va_start(args, format);
    vsnprintf(request.message, sizeof(request.message), format, args);
    va_end(args);
    return sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

static int
is_game_title_id(const char *value) {
    if(strncmp(value, "CUSA", 4) != 0 && strncmp(value, "PPSA", 4) != 0) {
        return 0;
    }
    for(size_t i = 4; i < TITLE_ID_LENGTH; i++) {
        if(!isdigit((unsigned char)value[i])) {
            return 0;
        }
    }
    return 1;
}

static int
extract_title_id(const char *line, char output[TITLE_ID_LENGTH + 1]) {
    size_t length = strlen(line);
    for(size_t i = 0; i + TITLE_ID_LENGTH <= length; i++) {
        if(is_game_title_id(line + i)) {
            memcpy(output, line + i, TITLE_ID_LENGTH);
            output[TITLE_ID_LENGTH] = '\0';
            return 1;
        }
    }
    return 0;
}

static int
extract_json_string(const char *json, const char *key, char *output,
                    size_t output_size) {
    char search[64];
    const char *cursor;
    size_t length = 0;

    snprintf(search, sizeof(search), "\"%s\"", key);
    cursor = strstr(json, search);
    if(!cursor || !(cursor = strchr(cursor + strlen(search), ':'))) {
        return 0;
    }
    while(*++cursor && isspace((unsigned char)*cursor)) {
        /* skip whitespace */
    }
    if(*cursor++ != '"') {
        return 0;
    }
    while(cursor[length] && cursor[length] != '"' && length + 1 < output_size) {
        output[length] = cursor[length];
        length++;
    }
    output[length] = '\0';
    return length > 0;
}

static int
read_title_name_file(const char *path, char output[MAX_TITLE_NAME]) {
    struct stat info;
    char *json;
    FILE *file;
    size_t bytes_read;
    const char *english;
    int found;

    if(stat(path, &info) != 0 || info.st_size <= 0
       || info.st_size > MAX_PARAM_JSON_SIZE) {
        return 0;
    }
    file = fopen(path, "rb");
    if(!file) {
        return 0;
    }
    json = malloc((size_t)info.st_size + 1);
    if(!json) {
        fclose(file);
        return 0;
    }
    bytes_read = fread(json, 1, (size_t)info.st_size, file);
    fclose(file);
    json[bytes_read] = '\0';
    english = strstr(json, "\"en-US\"");
    found = extract_json_string(english ? english : json, "titleName", output,
                                MAX_TITLE_NAME);
    if(!found && english) {
        found = extract_json_string(json, "titleName", output, MAX_TITLE_NAME);
    }
    free(json);
    return found;
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
read_title_name_sfo(const char *path, char output[MAX_TITLE_NAME]) {
    struct stat info;
    unsigned char *sfo;
    FILE *file;
    size_t bytes_read;
    uint32_t key_table;
    uint32_t data_table;
    uint32_t entry_count;
    int found = 0;

    if(stat(path, &info) != 0 || info.st_size < 20
       || info.st_size > MAX_PARAM_JSON_SIZE) {
        return 0;
    }
    file = fopen(path, "rb");
    if(!file) return 0;
    sfo = malloc((size_t)info.st_size);
    if(!sfo) {
        fclose(file);
        return 0;
    }
    bytes_read = fread(sfo, 1, (size_t)info.st_size, file);
    fclose(file);
    if(bytes_read < 20 || read_le32(sfo) != 0x46535000u) {
        free(sfo);
        return 0;
    }
    key_table = read_le32(sfo + 8);
    data_table = read_le32(sfo + 12);
    entry_count = read_le32(sfo + 16);
    if(key_table >= bytes_read || data_table >= bytes_read
       || entry_count > (bytes_read - 20) / 16) {
        free(sfo);
        return 0;
    }
    for(int pass = 0; pass < 2 && !found; pass++) {
        for(uint32_t i = 0; i < entry_count; i++) {
            const unsigned char *entry = sfo + 20 + i * 16;
            uint32_t key_offset = key_table + read_le16(entry);
            uint32_t value_length = read_le32(entry + 4);
            uint32_t value_offset = data_table + read_le32(entry + 12);
            const char *key;
            size_t copy_length;
            if(key_offset >= bytes_read || value_offset >= bytes_read
               || value_length == 0 || value_length > bytes_read - value_offset) {
                continue;
            }
            key = (const char *)(sfo + key_offset);
            if(!memchr(key, '\0', bytes_read - key_offset)) continue;
            if((pass == 0 && strcmp(key, "TITLE") != 0)
               || (pass == 1 && strncmp(key, "TITLE_", 6) != 0)) {
                continue;
            }
            copy_length = strnlen((const char *)(sfo + value_offset),
                                  value_length);
            if(copy_length >= MAX_TITLE_NAME) copy_length = MAX_TITLE_NAME - 1;
            if(copy_length) {
                memcpy(output, sfo + value_offset, copy_length);
                output[copy_length] = '\0';
                found = 1;
                break;
            }
        }
    }
    free(sfo);
    return found;
}

static int
load_title_name(const char *title_id, char output[MAX_TITLE_NAME]) {
    char path[512];
    const char *json_formats[] = {
        "/user/appmeta/%s/param.json",
        "/system_ex/app/%s/sce_sys/param.json",
        "/user/app/%s/sce_sys/param.json",
    };
    const char *sfo_formats[] = {
        "/system_data/priv/appmeta/%s/param.sfo",
        "/user/appmeta/%s/param.sfo",
        "/user/app/%s/sce_sys/param.sfo",
        "/system_ex/app/%s/sce_sys/param.sfo",
    };

    output[0] = '\0';
    for(size_t i = 0; i < sizeof(json_formats) / sizeof(json_formats[0]); i++) {
        snprintf(path, sizeof(path), json_formats[i], title_id);
        if(read_title_name_file(path, output)) {
            return 1;
        }
    }
    for(size_t i = 0; i < sizeof(sfo_formats) / sizeof(sfo_formats[0]); i++) {
        snprintf(path, sizeof(path), sfo_formats[i], title_id);
        if(read_title_name_sfo(path, output)) {
            return 1;
        }
    }
    return 0;
}

static void
json_write_escaped(FILE *file, const char *value) {
    for(const unsigned char *cursor = (const unsigned char *)value; *cursor; cursor++) {
        switch(*cursor) {
        case '"':
            fputs("\\\"", file);
            break;
        case '\\':
            fputs("\\\\", file);
            break;
        case '\n':
            fputs("\\n", file);
            break;
        case '\r':
            fputs("\\r", file);
            break;
        case '\t':
            fputs("\\t", file);
            break;
        default:
            if(*cursor >= 0x20) {
                fputc(*cursor, file);
            }
            break;
        }
    }
}

static void
write_event(const char *event, const char *title_id, uint32_t app_id,
            const char *raw, const char *title_name) {
    struct timespec monotonic;
    struct timespec realtime;
    struct tm utc;
    char timestamp[40];

    clock_gettime(CLOCK_MONOTONIC, &monotonic);
    clock_gettime(CLOCK_REALTIME, &realtime);
    gmtime_r(&realtime.tv_sec, &utc);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &utc);

    fprintf(event_file,
            "{\"time\":\"%s.%03ldZ\",\"monotonic_ms\":%lld,"
            "\"event\":\"",
            timestamp, realtime.tv_nsec / 1000000,
            (long long)monotonic.tv_sec * 1000 + monotonic.tv_nsec / 1000000);
    json_write_escaped(event_file, event);
    fputs("\"", event_file);
    if(title_id && title_id[0]) {
        fputs(",\"title_id\":\"", event_file);
        json_write_escaped(event_file, title_id);
        fputs("\"", event_file);
    }
    if(app_id) {
        fprintf(event_file, ",\"app_id\":\"0x%08x\"", app_id);
    }
    if(title_name && title_name[0]) {
        fputs(",\"title_name\":\"", event_file);
        json_write_escaped(event_file, title_name);
        fputs("\"", event_file);
    }
    if(raw && raw[0]) {
        fputs(",\"raw\":\"", event_file);
        json_write_escaped(event_file, raw);
        fputs("\"", event_file);
    }
    fputs("}\n", event_file);
    fflush(event_file);
    fsync(fileno(event_file));
    tracker_event(event, title_id, title_name,
                  (uint64_t)realtime.tv_sec * 1000
                      + (uint64_t)realtime.tv_nsec / 1000000);
    if(strcmp(event, "dashboard_http_error") == 0
       || strcmp(event, "fatal") == 0
       || strcmp(event, "parser_overflow") == 0) {
        tracker_diagnostic(
            strcmp(event, "fatal") == 0 ? "critical" : "error",
            event, title_id, raw && raw[0] ? raw : event,
            (uint64_t)realtime.tv_sec * 1000
                + (uint64_t)realtime.tv_nsec / 1000000);
    }
}

static int
contains_lower(const char *lower, const char *needle) {
    return strstr(lower, needle) != NULL;
}

static void
capture_diagnostic_line(const char *line, uint64_t now_ms) {
    char lower[1024];
    char title_id[TITLE_ID_LENGTH + 1] = {0};
    const char *level = NULL;
    const char *type = NULL;
    size_t length;
    if(!line || !line[0]) return;
    length = strlen(line);
    if(length >= sizeof(lower)) length = sizeof(lower) - 1;
    for(size_t i = 0; i < length; i++) {
        unsigned char value = (unsigned char)line[i];
        lower[i] = value >= 'A' && value <= 'Z'
            ? (char)(value - 'A' + 'a') : (char)value;
    }
    lower[length] = '\0';
    if(contains_lower(lower, "coredump")
       || contains_lower(lower, "core dump")
       || contains_lower(lower, "crash")
       || contains_lower(lower, "panic")) {
        level = "critical";
        type = "game_crash";
    } else if(contains_lower(lower, "license")
              && (contains_lower(lower, "error")
                  || contains_lower(lower, "fail")
                  || contains_lower(lower, "invalid")
                  || contains_lower(lower, "denied"))) {
        level = "error";
        type = "license_error";
    } else if((contains_lower(lower, "mount")
               || contains_lower(lower, "image"))
              && (contains_lower(lower, "error")
                  || contains_lower(lower, "fail")
                  || contains_lower(lower, "denied")
                  || contains_lower(lower, "read error"))) {
        level = "error";
        type = "mount_error";
    } else if((contains_lower(lower, "launch")
               || contains_lower(lower, "start app")
               || contains_lower(lower, "splashscreen"))
              && (contains_lower(lower, "error")
                  || contains_lower(lower, "fail")
                  || contains_lower(lower, "cannot"))) {
        level = "error";
        type = "launch_error";
    } else if(contains_lower(lower, "fatal")
              || contains_lower(lower, "failed")
              || (contains_lower(lower, "error")
                  && !contains_lower(lower, "error=0")
                  && !contains_lower(lower, "error: 0")
                  && !contains_lower(lower, "no error"))) {
        level = "error";
        type = "system_error";
    }
    if(!type) return;
    if(!extract_title_id(line, title_id) && foreground_title[0]) {
        memcpy(title_id, foreground_title, sizeof(title_id));
    }
    tracker_diagnostic(level, type, title_id[0] ? title_id : NULL,
                       line, now_ms);
}

static const char *
lookup_title(uint32_t app_id) {
    for(size_t i = 0; i < mapping_count; i++) {
        if(mappings[i].app_id == app_id) {
            return mappings[i].title_id;
        }
    }
    return NULL;
}

static void
remember_mapping(uint32_t app_id, const char *title_id, const char *raw) {
    if(!app_id || !title_id || !is_game_title_id(title_id)) {
        return;
    }
    for(size_t i = 0; i < mapping_count; i++) {
        if(mappings[i].app_id == app_id) {
            if(strcmp(mappings[i].title_id, title_id) != 0) {
                memcpy(mappings[i].title_id, title_id, TITLE_ID_LENGTH + 1);
                write_event("mapping", title_id, app_id, raw, NULL);
            }
            return;
        }
    }
    if(mapping_count == MAX_APP_MAPPINGS) {
        memmove(&mappings[0], &mappings[1],
                sizeof(mappings[0]) * (MAX_APP_MAPPINGS - 1));
        mapping_count--;
    }
    mappings[mapping_count].app_id = app_id;
    memcpy(mappings[mapping_count].title_id, title_id, TITLE_ID_LENGTH + 1);
    mapping_count++;
    write_event("mapping", title_id, app_id, raw, NULL);
}

static int
parse_focus_transition(const char *line, uint32_t *old_app_id,
                       uint32_t *new_app_id) {
    const char *marker = strstr(line, "FG App was changed.");
    unsigned int old_value;
    unsigned int new_value;

    if(!marker) {
        return 0;
    }
    marker += strlen("FG App was changed.");
    if(sscanf(marker, " %x -> %x", &old_value, &new_value) != 2) {
        return 0;
    }
    *old_app_id = (uint32_t)old_value;
    *new_app_id = (uint32_t)new_value;
    return 1;
}

static void
handle_focus_transition(uint32_t old_app_id, uint32_t new_app_id,
                        const char *line) {
    const char *old_title = lookup_title(old_app_id);
    const char *new_title;
    char title_name[MAX_TITLE_NAME];

    if(pending_title[0]) {
        if(!lookup_title(new_app_id)) {
            remember_mapping(new_app_id, pending_title, line);
        }
        pending_title[0] = '\0';
    }
    new_title = lookup_title(new_app_id);

    if(old_title && old_app_id != new_app_id
       && foreground_app_id == old_app_id) {
        write_event("background", old_title, old_app_id, line, NULL);
        foreground_app_id = 0;
        foreground_title[0] = '\0';
    }
    if(new_title && old_app_id != new_app_id
       && foreground_app_id != new_app_id) {
        if(load_title_name(new_title, title_name)) {
            write_event("metadata", new_title, new_app_id, NULL, title_name);
        }
        write_event("foreground", new_title, new_app_id, line, NULL);
        foreground_app_id = new_app_id;
        memcpy(foreground_title, new_title, TITLE_ID_LENGTH + 1);
    }
}

static void
handle_line(char *line) {
    char title_id[TITLE_ID_LENGTH + 1] = {0};
    uint32_t old_app_id;
    uint32_t new_app_id;
    unsigned int app_value;
    const char *app_marker;
    char title_name[MAX_TITLE_NAME];
    struct timespec now;

    clock_gettime(CLOCK_REALTIME, &now);
    tracker_tick((uint64_t)now.tv_sec * 1000
                 + (uint64_t)now.tv_nsec / 1000000);

    if(access(STOP_PATH, F_OK) == 0) {
        stop_requested = 1;
        return;
    }
    if(ENABLE_DIAGNOSTIC_CAPTURE) {
        capture_diagnostic_line(
            line, (uint64_t)now.tv_sec * 1000
                + (uint64_t)now.tv_nsec / 1000000);
    }

    if((strstr(line, "-> [SplashScreen.")
        || strstr(line, "SceneQ : Loaded["))
       && extract_title_id(line, title_id)) {
        memcpy(pending_title, title_id, sizeof(pending_title));
        write_event("launch_detected", title_id, 0, line, NULL);
        if(strstr(line, "-> [SplashScreen.")
           && load_title_name(title_id, title_name)) {
            write_event("metadata", title_id, 0, NULL, title_name);
        }
    }

    if(parse_focus_transition(line, &old_app_id, &new_app_id)) {
        handle_focus_transition(old_app_id, new_app_id, line);
        return;
    }

    app_marker = strstr(line, "appId={");
    if(strstr(line, "BlockingKill()") && strstr(line, "titleId={")
       && app_marker && extract_title_id(line, title_id)
       && sscanf(app_marker + strlen("appId={"), "0x%x", &app_value) == 1) {
        remember_mapping((uint32_t)app_value, title_id, line);
        write_event("exit", title_id, (uint32_t)app_value, line, NULL);
        if(foreground_app_id == (uint32_t)app_value) {
            foreground_app_id = 0;
            foreground_title[0] = '\0';
        }
        return;
    }

    if(strstr(line, "[Syscore App] Kill App :")
       && extract_title_id(line, title_id)) {
        write_event("exit_signal", title_id, 0, line, NULL);
    }
}

static int
acquire_single_instance(void) {
    char pid[32];
    int length;
    instance_fd = open(PID_PATH, O_RDWR | O_CREAT, 0644);
    if(instance_fd < 0 || flock(instance_fd, LOCK_EX | LOCK_NB) != 0) {
        if(instance_fd >= 0) close(instance_fd);
        instance_fd = -1;
        return -1;
    }
    length = snprintf(pid, sizeof(pid), "%d\n", getpid());
    if(length <= 0 || ftruncate(instance_fd, 0) != 0
       || lseek(instance_fd, 0, SEEK_SET) < 0
       || write(instance_fd, pid, (size_t)length) != (ssize_t)length
       || fsync(instance_fd) != 0) {
        close(instance_fd);
        instance_fd = -1;
        return -1;
    }
    return 0;
}

static void
release_single_instance(void) {
    if(instance_fd >= 0) {
        flock(instance_fd, LOCK_UN);
        close(instance_fd);
        instance_fd = -1;
    }
    unlink(PID_PATH);
}

int
main(void) {
    char read_buffer[1024];
    char line_buffer[LINE_CAPACITY];
    size_t line_length = 0;
    ssize_t bytes_read;
    int klog_fd;
    int notify_result;
    int focus_available;
    int power_available;
    char notify_status[64];
    app_focus_monitor_t focus_monitor;
    power_monitor_t power_monitor;
    struct timespec startup_pause = {STARTUP_DELAY_SECONDS, 0};

    if(mkdir(DATA_DIR, 0755) < 0 && errno != EEXIST) {
        notify("Playlog: cannot create %s (%s)", DATA_DIR,
               strerror(errno));
        return 1;
    }
    nanosleep(&startup_pause, NULL);
    unlink("/data/etaHEN/plugins/ps5-activity-tracker.elf");
    unlink("/data/etaHEN/plugins/ps5-activity-tracker.elf.auto_start");
    if(acquire_single_instance() != 0) {
        notify("Playlog is already running");
        return 1;
    }
    unlink(STOP_PATH);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);
    {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        tracker_init((uint64_t)now.tv_sec * 1000
                     + (uint64_t)now.tv_nsec / 1000000);
    }
    event_file = fopen(EVENT_PATH, "a");
    if(!event_file) {
        notify("Playlog: cannot open event log (%s)", strerror(errno));
        release_single_instance();
        return 1;
    }
    if(dashboard_http_start() == 0) {
        write_event("dashboard_http", NULL, 0,
                    "http://127.0.0.1:12888/", NULL);
    } else {
        write_event("dashboard_http_error", NULL, 0,
                    "cannot start local server", NULL);
    }
    klog_fd = open(KLOG_PATH, O_RDONLY | O_NONBLOCK);
    if(klog_fd < 0) {
        write_event("fatal", NULL, 0, "cannot open /dev/klog", NULL);
        notify("Playlog: cannot open /dev/klog (%s)", strerror(errno));
        dashboard_http_stop();
        fclose(event_file);
        release_single_instance();
        return 1;
    }
    focus_available = app_focus_monitor_open(&focus_monitor) == 0;
    write_event(focus_available ? "app_focus_monitor" :
                "app_focus_monitor_unavailable", NULL, 0,
                "SceShellCoreUtilAppFocus", NULL);
    power_available = power_monitor_open(&power_monitor) == 0;
    write_event(power_available ? "power_monitor" :
                "power_monitor_unavailable", NULL, 0,
                "SceSystemStateMgrInfo+SceSystemStateMgrStatus", NULL);

    write_event("probe_start", NULL, 0, "version=" PROBE_VERSION, NULL);
    notify_result = notify("Playlog %s started", TRACKER_VERSION);
    snprintf(notify_status, sizeof(notify_status), "return=%d", notify_result);
    write_event("notification_result", NULL, 0, notify_status, NULL);

    while(!stop_requested) {
        bytes_read = read(klog_fd, read_buffer, sizeof(read_buffer));
        if(bytes_read > 0) {
            for(ssize_t i = 0; i < bytes_read; i++) {
                char value = read_buffer[i];
                if(value == '\n') {
                    line_buffer[line_length] = '\0';
                    handle_line(line_buffer);
                    line_length = 0;
                    if(stop_requested) {
                        break;
                    }
                } else if(value != '\r') {
                    if(line_length + 1 < sizeof(line_buffer)) {
                        line_buffer[line_length++] = value;
                    } else {
                        line_length = 0;
                        write_event("parser_overflow", NULL, 0, NULL, NULL);
                    }
                }
            }
        } else if(bytes_read < 0 && errno != EAGAIN
                  && errno != EWOULDBLOCK && errno != EINTR) {
            break;
        }
        if(focus_available) {
            uint32_t old_app_id;
            uint32_t new_app_id;
            if(app_focus_monitor_poll(
                   &focus_monitor, &old_app_id, &new_app_id) > 0) {
                char source[128];
                snprintf(source, sizeof(source),
                         "[SceShellCoreUtilAppFocus] 0x%08x -> 0x%08x",
                         old_app_id, new_app_id);
                handle_focus_transition(old_app_id, new_app_id, source);
            }
        }
        if(power_available) {
            const char *reason = NULL;
            if(power_monitor_poll(&power_monitor, &reason) > 0) {
                snprintf(stop_reason, sizeof(stop_reason), "%s",
                         reason ? reason : "system power transition");
                write_event("suspend_prepare", foreground_title,
                            foreground_app_id, stop_reason, NULL);
                stop_requested = 1;
            }
        }
        {
            struct timespec now;
            struct timespec pause = {0, IDLE_POLL_NANOSECONDS};
            clock_gettime(CLOCK_REALTIME, &now);
            tracker_tick((uint64_t)now.tv_sec * 1000
                         + (uint64_t)now.tv_nsec / 1000000);
            if(access(STOP_PATH, F_OK) == 0) {
                snprintf(stop_reason, sizeof(stop_reason), "stop file");
                stop_requested = 1;
            }
            if(bytes_read <= 0 && !stop_requested) nanosleep(&pause, NULL);
        }
    }

    if(received_signal) {
        snprintf(stop_reason, sizeof(stop_reason), "signal=%d",
                 (int)received_signal);
    } else if(!stop_requested) {
        snprintf(stop_reason, sizeof(stop_reason), "%s",
                 bytes_read < 0 ? strerror(errno) : "klog EOF");
    }
    write_event("probe_stop", foreground_title, foreground_app_id,
                stop_reason, NULL);
    {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        tracker_shutdown((uint64_t)now.tv_sec * 1000
                         + (uint64_t)now.tv_nsec / 1000000);
    }
    dashboard_http_stop();
    app_focus_monitor_close(&focus_monitor);
    power_monitor_close(&power_monitor);
    close(klog_fd);
    fclose(event_file);
    release_single_instance();
    return bytes_read < 0 ? 1 : 0;
}
