#include "tracker.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef PS5_BUILD
#include <ps5/kernel.h>
#endif

#define STATE_MAGIC 0x50535441u
#define STATE_VERSION 1u
#define STATE_FILE_MAGIC 0x50534653u
#define STATE_FILE_VERSION 2u
#define MAX_GAMES 128
#define MAX_SESSIONS 1024
#define TITLE_ID_SIZE 10
#define TITLE_NAME_SIZE 256
#define WRITE_INTERVAL_MS 60000ULL
#define MAX_REASONABLE_TIME_MS (20ULL * 365 * 24 * 60 * 60 * 1000)
#define COMPLETED_MAGIC 0x50434f4du
#define COMPLETED_VERSION 1u
#define DIAGNOSTICS_MAGIC 0x50444941u
#define DIAGNOSTICS_VERSION 1u
#define MAX_DIAGNOSTICS 10
#define DIAGNOSTIC_TYPE_SIZE 32
#define DIAGNOSTIC_LEVEL_SIZE 12
#define DIAGNOSTIC_MESSAGE_SIZE 256
#define DIAGNOSTIC_DEDUP_MS 60000ULL
#define BACKUP_MAGIC 0x5042414bu
#define BACKUP_VERSION 1u
#define BACKUP_FILE_COUNT 4
#ifndef TRACKER_VERSION
#define TRACKER_VERSION "dev"
#endif
#ifndef PROBE_VERSION
#define PROBE_VERSION "dev"
#endif

typedef struct {
    char title_id[TITLE_ID_SIZE];
    char name[TITLE_NAME_SIZE];
    uint32_t session_count;
    uint64_t active_ms;
    uint64_t paused_ms;
    uint64_t first_played_ms;
    uint64_t last_played_ms;
} tracker_game_t;

typedef struct {
    uint16_t game_index;
    uint8_t is_open;
    uint8_t is_active;
    uint64_t started_ms;
    uint64_t ended_ms;
    uint64_t active_ms;
    uint64_t paused_ms;
    uint64_t mark_ms;
} tracker_session_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t game_count;
    uint32_t session_count;
    int32_t current_session;
    uint64_t last_write_ms;
    tracker_game_t games[MAX_GAMES];
    tracker_session_t sessions[MAX_SESSIONS];
} tracker_state_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t payload_size;
    uint32_t crc32;
    uint64_t generation;
} state_file_header_t;

typedef struct {
    char title_id[TITLE_ID_SIZE];
    uint64_t completed_ms;
} completed_game_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    completed_game_t games[MAX_GAMES];
} completed_state_t;

typedef struct {
    char level[DIAGNOSTIC_LEVEL_SIZE];
    char type[DIAGNOSTIC_TYPE_SIZE];
    char title_id[TITLE_ID_SIZE];
    char message[DIAGNOSTIC_MESSAGE_SIZE];
    uint64_t occurred_ms;
    uint32_t count;
} diagnostic_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    diagnostic_entry_t entries[MAX_DIAGNOSTICS];
} diagnostics_state_t;

typedef struct {
    uint32_t present;
    uint32_t crc32;
    uint64_t size;
} backup_file_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t created_ms;
    uint32_t game_count;
    uint32_t session_count;
    uint64_t active_ms;
    backup_file_t files[BACKUP_FILE_COUNT];
} backup_meta_t;

static tracker_state_t state;
static uint64_t state_generation;
static completed_state_t completed_state;
static diagnostics_state_t diagnostics_state;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static int timezone_offset_minutes;
static char timezone_name[64] = "System";
static char timezone_source[16] = "system";
static char firmware_version[16] = "unknown";
static char console_ip[INET_ADDRSTRLEN] = "unavailable";

static void
detect_console_ip(void) {
    struct ifaddrs *addresses = NULL;
    struct ifaddrs *current;
    if(getifaddrs(&addresses) != 0) return;
    for(current = addresses; current; current = current->ifa_next) {
        struct sockaddr_in *ipv4;
        uint32_t address;
        if(!current->ifa_addr || current->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        ipv4 = (struct sockaddr_in *)current->ifa_addr;
        address = ntohl(ipv4->sin_addr.s_addr);
        if((address >> 24) == 127 || address == 0) continue;
        if(inet_ntop(AF_INET, &ipv4->sin_addr,
                     console_ip, sizeof(console_ip))) break;
    }
    freeifaddrs(addresses);
}

static void
format_offset_name(char output[64], int minutes) {
    int absolute = minutes < 0 ? -minutes : minutes;
    snprintf(output, 64, "UTC%c%02d:%02d", minutes < 0 ? '-' : '+',
             absolute / 60, absolute % 60);
}

static void
detect_firmware(void) {
#ifdef PS5_BUILD
    uint32_t value = kernel_get_fw_version();
    unsigned major = (value >> 24) & 0xff;
    unsigned minor = (value >> 16) & 0xff;
    if(value && major) {
        snprintf(firmware_version, sizeof(firmware_version),
                 "%x.%02x", major, minor);
    }
#endif
}

static void
load_config(uint64_t now_ms) {
    FILE *file;
    char buffer[2048];
    size_t length;
    char *marker;
    int offset;
    (void)now_ms;
    timezone_offset_minutes = 180;
    snprintf(timezone_name, sizeof(timezone_name), "Europe/Moscow");
    snprintf(timezone_source, sizeof(timezone_source), "fallback");
    file = fopen(TRACKER_CONFIG_PATH, "r");
    if(!file) return;
    length = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[length] = '\0';
    marker = strstr(buffer, "\"timezone_offset_minutes\"");
    if(marker && (marker = strchr(marker, ':'))
       && sscanf(marker + 1, "%d", &offset) == 1
       && offset >= -720 && offset <= 840) {
        timezone_offset_minutes = offset;
        snprintf(timezone_source, sizeof(timezone_source), "config");
        format_offset_name(timezone_name, offset);
        marker = strstr(buffer, "\"timezone_name\"");
        if(marker && (marker = strchr(marker, ':'))
           && (marker = strchr(marker, '"'))) {
            char *end;
            marker++;
            end = strchr(marker, '"');
            if(end && end > marker
               && (size_t)(end - marker) < sizeof(timezone_name)) {
                memcpy(timezone_name, marker, (size_t)(end - marker));
                timezone_name[end - marker] = '\0';
            }
        }
    }
    marker = strstr(buffer, "\"firmware\"");
    if(marker && (marker = strchr(marker, ':'))
       && (marker = strchr(marker, '"'))) {
        char *end;
        marker++;
        end = strchr(marker, '"');
        if(end && end > marker
           && (size_t)(end - marker) < sizeof(firmware_version)) {
            memcpy(firmware_version, marker, (size_t)(end - marker));
            firmware_version[end - marker] = '\0';
        }
    }
}

static uint64_t
completed_ms(const char *title_id) {
    for(uint32_t i = 0; i < completed_state.count; i++) {
        if(strcmp(completed_state.games[i].title_id, title_id) == 0) {
            return completed_state.games[i].completed_ms;
        }
    }
    return 0;
}

static int
write_completed_state(void) {
    const char *temporary = TRACKER_COMPLETED_PATH ".tmp";
    FILE *file = fopen(temporary, "wb");
    if(!file) return -1;
    if(fwrite(&completed_state, sizeof(completed_state), 1, file) != 1
       || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        fclose(file);
        unlink(temporary);
        return -1;
    }
    fclose(file);
    if(rename(temporary, TRACKER_COMPLETED_PATH) != 0) {
        unlink(temporary);
        return -1;
    }
    chmod(TRACKER_COMPLETED_PATH, 0644);
    return 0;
}

static void
load_completed_state(void) {
    FILE *file = fopen(TRACKER_COMPLETED_PATH, "rb");
    completed_state_t loaded;
    memset(&completed_state, 0, sizeof(completed_state));
    if(file) {
        if(fread(&loaded, sizeof(loaded), 1, file) == 1
           && loaded.magic == COMPLETED_MAGIC
           && loaded.version == COMPLETED_VERSION
           && loaded.count <= MAX_GAMES) {
            completed_state = loaded;
        }
        fclose(file);
    }
    completed_state.magic = COMPLETED_MAGIC;
    completed_state.version = COMPLETED_VERSION;
}

static int
write_diagnostics_state(void) {
    const char *temporary = TRACKER_DIAGNOSTICS_PATH ".tmp";
    FILE *file = fopen(temporary, "wb");
    if(!file) return -1;
    if(fwrite(&diagnostics_state, sizeof(diagnostics_state), 1, file) != 1
       || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        fclose(file);
        unlink(temporary);
        return -1;
    }
    fclose(file);
    if(rename(temporary, TRACKER_DIAGNOSTICS_PATH) != 0) {
        unlink(temporary);
        return -1;
    }
    chmod(TRACKER_DIAGNOSTICS_PATH, 0644);
    return 0;
}

static void
load_diagnostics_state(void) {
    FILE *file = fopen(TRACKER_DIAGNOSTICS_PATH, "rb");
    diagnostics_state_t loaded;
    memset(&diagnostics_state, 0, sizeof(diagnostics_state));
    if(file) {
        if(fread(&loaded, sizeof(loaded), 1, file) == 1
           && loaded.magic == DIAGNOSTICS_MAGIC
           && loaded.version == DIAGNOSTICS_VERSION
           && loaded.count <= MAX_DIAGNOSTICS) {
            diagnostics_state = loaded;
        }
        fclose(file);
    }
    diagnostics_state.magic = DIAGNOSTICS_MAGIC;
    diagnostics_state.version = DIAGNOSTICS_VERSION;
}

static void
json_escape(FILE *file, const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;
    while(cursor && *cursor) {
        switch(*cursor) {
        case '"': fputs("\\\"", file); break;
        case '\\': fputs("\\\\", file); break;
        case '\n': fputs("\\n", file); break;
        case '\r': fputs("\\r", file); break;
        case '\t': fputs("\\t", file); break;
        default:
            if(*cursor >= 0x20) {
                fputc(*cursor, file);
            }
            break;
        }
        cursor++;
    }
}

static int
valid_config_text(const char *value, size_t maximum) {
    size_t length;
    if(!value || !(length = strlen(value)) || length >= maximum) return 0;
    for(size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
             || (c >= '0' && c <= '9') || c == '/' || c == '_'
             || c == '-' || c == '+' || c == '.' || c == ' ')) {
            return 0;
        }
    }
    return 1;
}

static int
write_config_file(void) {
    const char *temporary = TRACKER_CONFIG_PATH ".tmp";
    FILE *file = fopen(temporary, "w");
    if(!file) return -1;
    fprintf(file, "{\n  \"timezone_offset_minutes\": %d,\n"
            "  \"timezone_name\": \"", timezone_offset_minutes);
    json_escape(file, timezone_name);
    fputs("\",\n  \"firmware\": \"", file);
    json_escape(file, firmware_version);
    fputs("\"\n}\n", file);
    if(fflush(file) != 0 || fsync(fileno(file)) != 0) {
        fclose(file);
        unlink(temporary);
        return -1;
    }
    fclose(file);
    if(rename(temporary, TRACKER_CONFIG_PATH) != 0) {
        unlink(temporary);
        return -1;
    }
    chmod(TRACKER_CONFIG_PATH, 0644);
    return 0;
}

static void
iso_time(char output[32], uint64_t milliseconds) {
    time_t seconds = (time_t)(milliseconds / 1000);
    struct tm utc;
    gmtime_r(&seconds, &utc);
    strftime(output, 24, "%Y-%m-%dT%H:%M:%S", &utc);
    snprintf(output + 19, 13, ".%03lluZ",
             (unsigned long long)(milliseconds % 1000));
}

static void
local_iso_time(char output[32], uint64_t milliseconds) {
    int offset = timezone_offset_minutes;
    int absolute = offset < 0 ? -offset : offset;
    time_t seconds = (time_t)(milliseconds / 1000 + offset * 60);
    struct tm local;
    gmtime_r(&seconds, &local);
    strftime(output, 24, "%Y-%m-%dT%H:%M:%S", &local);
    snprintf(output + 19, 13, ".%03llu%c%02d:%02d",
             (unsigned long long)(milliseconds % 1000),
             offset < 0 ? '-' : '+', absolute / 60, absolute % 60);
}

static const char *
display_name(const tracker_game_t *game) {
    return game->name[0] ? game->name : game->title_id;
}

static int
game_index(const char *title_id, int create) {
    uint32_t index;
    if(!title_id || !title_id[0]) {
        return -1;
    }
    for(index = 0; index < state.game_count; index++) {
        if(strcmp(state.games[index].title_id, title_id) == 0) {
            return (int)index;
        }
    }
    if(!create || state.game_count >= MAX_GAMES) {
        return -1;
    }
    index = state.game_count++;
    memset(&state.games[index], 0, sizeof(state.games[index]));
    snprintf(state.games[index].title_id,
             sizeof(state.games[index].title_id), "%s", title_id);
    return (int)index;
}

static void
checkpoint(uint64_t now_ms) {
    tracker_session_t *session;
    tracker_game_t *game;
    uint64_t delta;
    if(state.current_session < 0
       || (uint32_t)state.current_session >= state.session_count) {
        return;
    }
    session = &state.sessions[state.current_session];
    if(!session->is_open || now_ms <= session->mark_ms) {
        return;
    }
    delta = now_ms - session->mark_ms;
    game = &state.games[session->game_index];
    if(session->is_active) {
        session->active_ms += delta;
        game->active_ms += delta;
    } else {
        session->paused_ms += delta;
        game->paused_ms += delta;
    }
    session->mark_ms = now_ms;
    session->ended_ms = now_ms;
    game->last_played_ms = now_ms;
}

static void
close_current(uint64_t now_ms) {
    tracker_session_t *session;
    checkpoint(now_ms);
    if(state.current_session < 0
       || (uint32_t)state.current_session >= state.session_count) {
        return;
    }
    session = &state.sessions[state.current_session];
    session->is_open = 0;
    session->is_active = 0;
    session->ended_ms = now_ms;
    state.current_session = -1;
}

static uint32_t
state_crc32(const void *data, size_t length) {
    const unsigned char *cursor = data;
    uint32_t crc = 0xffffffffu;
    while(length--) {
        crc ^= *cursor++;
        for(int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static int
valid_title_id(const char *value) {
    if(!memchr(value, '\0', TITLE_ID_SIZE)
       || strlen(value) != TITLE_ID_SIZE - 1
       || (strncmp(value, "CUSA", 4) != 0
           && strncmp(value, "PPSA", 4) != 0)) {
        return 0;
    }
    for(int i = 4; i < TITLE_ID_SIZE - 1; i++) {
        if(value[i] < '0' || value[i] > '9') return 0;
    }
    return 1;
}

static int
validate_state(const tracker_state_t *candidate) {
    if(candidate->magic != STATE_MAGIC
       || candidate->version != STATE_VERSION
       || candidate->game_count > MAX_GAMES
       || candidate->session_count > MAX_SESSIONS
       || candidate->current_session < -1
       || (candidate->current_session >= 0
           && (uint32_t)candidate->current_session
               >= candidate->session_count)) {
        return 0;
    }
    for(uint32_t i = 0; i < candidate->game_count; i++) {
        const tracker_game_t *game = &candidate->games[i];
        if(!valid_title_id(game->title_id)
           || !memchr(game->name, '\0', TITLE_NAME_SIZE)
           || game->active_ms > MAX_REASONABLE_TIME_MS
           || game->paused_ms > MAX_REASONABLE_TIME_MS
           || (game->first_played_ms && game->last_played_ms
               && game->first_played_ms > game->last_played_ms)) {
            return 0;
        }
    }
    for(uint32_t i = 0; i < candidate->session_count; i++) {
        const tracker_session_t *session = &candidate->sessions[i];
        if(session->game_index >= candidate->game_count
           || session->is_open > 1 || session->is_active > 1
           || session->started_ms > session->ended_ms
           || session->active_ms > MAX_REASONABLE_TIME_MS
           || session->paused_ms > MAX_REASONABLE_TIME_MS
           || (session->is_active && !session->is_open)) {
            return 0;
        }
    }
    if(candidate->current_session >= 0
       && !candidate->sessions[candidate->current_session].is_open) {
        return 0;
    }
    return 1;
}

static int
load_state_file(const char *path, tracker_state_t *output,
                uint64_t *generation, int *legacy) {
    struct stat info;
    FILE *file;
    state_file_header_t header;
    if(stat(path, &info) != 0 || !(file = fopen(path, "rb"))) return -1;
    *legacy = 0;
    *generation = 0;
    if((size_t)info.st_size == sizeof(*output)) {
        if(fread(output, sizeof(*output), 1, file) != 1) {
            fclose(file);
            return -1;
        }
        *legacy = 1;
    } else if((size_t)info.st_size
              == sizeof(header) + sizeof(*output)) {
        if(fread(&header, sizeof(header), 1, file) != 1
           || header.magic != STATE_FILE_MAGIC
           || header.version != STATE_FILE_VERSION
           || header.payload_size != sizeof(*output)
           || fread(output, sizeof(*output), 1, file) != 1
           || header.crc32 != state_crc32(output, sizeof(*output))) {
            fclose(file);
            return -1;
        }
        *generation = header.generation;
    } else {
        fclose(file);
        return -1;
    }
    fclose(file);
    return validate_state(output) ? 0 : -1;
}

static int
write_state(void) {
    const char *temporary = TRACKER_STATE_PATH ".tmp";
    state_file_header_t header;
    static tracker_state_t verified;
    uint64_t verified_generation;
    int verified_legacy;
    FILE *file = fopen(temporary, "wb");
    if(!file) {
        return -1;
    }
    header.magic = STATE_FILE_MAGIC;
    header.version = STATE_FILE_VERSION;
    header.payload_size = sizeof(state);
    header.generation = state_generation + 1;
    header.crc32 = state_crc32(&state, sizeof(state));
    if(fwrite(&header, sizeof(header), 1, file) != 1
       || fwrite(&state, sizeof(state), 1, file) != 1
       || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        fclose(file);
        unlink(temporary);
        return -1;
    }
    fclose(file);
    if(load_state_file(temporary, &verified, &verified_generation,
                       &verified_legacy) != 0
       || verified_legacy
       || verified_generation != header.generation) {
        unlink(temporary);
        return -1;
    }
    if(access(TRACKER_STATE_PATH, F_OK) == 0) {
        unlink(TRACKER_PREVIOUS_STATE_PATH);
        if(rename(TRACKER_STATE_PATH, TRACKER_PREVIOUS_STATE_PATH) != 0) {
            unlink(temporary);
            return -1;
        }
    }
    if(rename(temporary, TRACKER_STATE_PATH) != 0) {
        if(access(TRACKER_PREVIOUS_STATE_PATH, F_OK) == 0) {
            rename(TRACKER_PREVIOUS_STATE_PATH, TRACKER_STATE_PATH);
        }
        unlink(temporary);
        return -1;
    }
    state_generation = header.generation;
    chmod(TRACKER_STATE_PATH, 0644);
    chmod(TRACKER_PREVIOUS_STATE_PATH, 0644);
    return 0;
}

static void
write_game(FILE *file, uint32_t index, double today_active,
           unsigned today_sessions) {
    tracker_game_t *game = &state.games[index];
    uint32_t platform_ps5 = strncmp(game->title_id, "PPSA", 4) == 0;
    char first[32] = "", first_local[32] = "";
    char last[32] = "", last_local[32] = "";
    char completed[32] = "";
    uint64_t completion_ms = completed_ms(game->title_id);
    if(game->first_played_ms) iso_time(first, game->first_played_ms);
    if(game->first_played_ms) local_iso_time(first_local, game->first_played_ms);
    if(game->last_played_ms) iso_time(last, game->last_played_ms);
    if(game->last_played_ms) local_iso_time(last_local, game->last_played_ms);
    if(completion_ms) iso_time(completed, completion_ms);
    fputs("{\"title_id\":\"", file); json_escape(file, game->title_id);
    fprintf(file, "\",\"platform\":\"%s\",\"name\":\"",
            platform_ps5 ? "PS5" : "PS4");
    json_escape(file, display_name(game));
    fputs("\",\"completed_at\":", file);
    if(completed[0]) fprintf(file, "\"%s\"", completed);
    else fputs("null", file);
    fprintf(file,
            ",\"session_count\":%u,\"today_active_seconds\":%.3f,"
            "\"today_session_count\":%u,"
            "\"active_seconds\":%.3f,\"average_session_seconds\":%.3f,"
            "\"first_played_at\":",
            game->session_count, today_active, today_sessions,
            game->active_ms / 1000.0,
            game->session_count
                ? game->active_ms / 1000.0 / game->session_count : 0.0);
    if(first[0]) fprintf(file, "\"%s\"", first); else fputs("null", file);
    fputs(",\"first_played_at_local\":", file);
    if(first_local[0]) fprintf(file, "\"%s\"", first_local);
    else fputs("null", file);
    fputs(",\"last_played_at\":", file);
    if(last[0]) fprintf(file, "\"%s\"", last); else fputs("null", file);
    fputs(",\"last_played_at_local\":", file);
    if(last_local[0]) fprintf(file, "\"%s\"", last_local);
    else fputs("null", file);
    fprintf(file, ",\"completion_seconds\":%.3f",
            completion_ms > game->first_played_ms
                ? (completion_ms - game->first_played_ms) / 1000.0 : 0.0);
    fputs(",\"median_load_seconds\":null,"
          "\"crash_count\":0,\"source_kind\":\"unknown\","
          "\"storage\":\"unknown\"}", file);
}

static void
write_session(FILE *file, const tracker_session_t *session) {
    tracker_game_t *game = &state.games[session->game_index];
    char started[32], ended[32], started_local[32], ended_local[32];
    iso_time(started, session->started_ms);
    iso_time(ended, session->ended_ms);
    local_iso_time(started_local, session->started_ms);
    local_iso_time(ended_local, session->ended_ms);
    fputs("{\"title_id\":\"", file); json_escape(file, game->title_id);
    fputs("\",\"name\":\"", file); json_escape(file, display_name(game));
    fprintf(file, "\",\"platform\":\"%s\",\"started_at\":\"%s\","
            "\"started_at_local\":\"%s\",\"ended_at\":\"%s\","
            "\"ended_at_local\":\"%s\",\"active_seconds\":%.3f,"
            "\"paused_seconds\":%.3f,\"is_open\":%u,\"is_active\":%u}",
            strncmp(game->title_id, "PPSA", 4) == 0 ? "PS5" : "PS4",
            started, started_local, ended, ended_local,
            session->active_ms / 1000.0,
            session->paused_ms / 1000.0, session->is_open,
            session->is_active);
}

static void
tracker_local_tm(uint64_t milliseconds, struct tm *output) {
    time_t seconds = (time_t)(milliseconds / 1000
                              + timezone_offset_minutes * 60);
    gmtime_r(&seconds, output);
}

static uint64_t
overlap_ms(uint64_t left_start, uint64_t left_end,
           uint64_t right_start, uint64_t right_end) {
    uint64_t start = left_start > right_start ? left_start : right_start;
    uint64_t end = left_end < right_end ? left_end : right_end;
    return end > start ? end - start : 0;
}

static void
write_period(FILE *file, uint64_t now_ms, int days, int hourly) {
    int buckets = hourly ? 24 : days;
    double active[30] = {0};
    double paused[30] = {0};
    unsigned sessions = 0;
    struct tm now_tm;
    time_t now_local;
    time_t period_start_local;
    tracker_local_tm(now_ms, &now_tm);
    now_local = (time_t)(now_ms / 1000 + timezone_offset_minutes * 60);
    period_start_local = now_local
        - (now_tm.tm_hour * 3600 + now_tm.tm_min * 60 + now_tm.tm_sec)
        - (hourly ? 0 : (days - 1) * 86400);

    for(uint32_t i = 0; i < state.session_count; i++) {
        tracker_session_t *session = &state.sessions[i];
        uint64_t elapsed = session->ended_ms > session->started_ms
            ? session->ended_ms - session->started_ms : 0;
        int hit = 0;
        if(!elapsed) continue;
        for(int bucket = 0; bucket < buckets; bucket++) {
            time_t bucket_start_local = period_start_local
                + bucket * (hourly ? 3600 : 86400);
            uint64_t bucket_start = (uint64_t)
                (bucket_start_local - timezone_offset_minutes * 60) * 1000;
            uint64_t bucket_end = bucket_start
                + (uint64_t)(hourly ? 3600 : 86400) * 1000;
            uint64_t overlap = overlap_ms(
                session->started_ms, session->ended_ms,
                bucket_start, bucket_end);
            if(overlap) {
                active[bucket] += session->active_ms / 1000.0
                    * (double)overlap / elapsed;
                paused[bucket] += session->paused_ms / 1000.0
                    * (double)overlap / elapsed;
                hit = 1;
            }
        }
        if(hit) sessions++;
    }
    double total_active = 0, total_paused = 0;
    for(int i = 0; i < buckets; i++) {
        total_active += active[i];
        total_paused += paused[i];
    }
    fprintf(file,
            "{\"active_seconds\":%.3f,\"paused_seconds\":%.3f,"
            "\"session_count\":%u,\"home_share_percent\":%.1f,\"series\":[",
            total_active, total_paused, sessions,
            total_active + total_paused
                ? total_paused * 100.0 / (total_active + total_paused) : 0.0);
    for(int i = 0; i < buckets; i++) {
        char label[8];
        if(i) fputc(',', file);
        if(hourly) {
            snprintf(label, sizeof(label), "%02d", i);
        } else {
            time_t day = period_start_local + i * 86400;
            struct tm value;
            gmtime_r(&day, &value);
            snprintf(label, sizeof(label), "%02d.%02d",
                     value.tm_mday, value.tm_mon + 1);
        }
        fprintf(file,
                "{\"label\":\"%s\",\"active_seconds\":%.3f,"
                "\"paused_seconds\":%.3f}",
                label, active[i], paused[i]);
    }
    fputs("]}", file);
}

static void
write_days(FILE *file, uint64_t now_ms) {
    time_t now_local = (time_t)(now_ms / 1000
                                + timezone_offset_minutes * 60);
    struct tm now_tm;
    time_t today_start_local;
    tracker_local_tm(now_ms, &now_tm);
    today_start_local = now_local
        - (now_tm.tm_hour * 3600 + now_tm.tm_min * 60 + now_tm.tm_sec);
    for(int day_index = 13; day_index >= 0; day_index--) {
        time_t day_time = today_start_local - day_index * 86400;
        struct tm day;
        double active = 0;
        unsigned sessions = 0;
        uint64_t day_start = (uint64_t)
            (day_time - timezone_offset_minutes * 60) * 1000;
        uint64_t day_end = day_start + 86400ULL * 1000;
        gmtime_r(&day_time, &day);
        for(uint32_t i = 0; i < state.session_count; i++) {
            tracker_session_t *session = &state.sessions[i];
            uint64_t elapsed = session->ended_ms > session->started_ms
                ? session->ended_ms - session->started_ms : 0;
            uint64_t overlap = overlap_ms(
                session->started_ms, session->ended_ms,
                day_start, day_end);
            if(elapsed && overlap) {
                active += session->active_ms / 1000.0
                    * (double)overlap / elapsed;
                sessions++;
            }
        }
        if(day_index != 13) fputc(',', file);
        fprintf(file,
                "{\"date\":\"%04d-%02d-%02d\",\"active_seconds\":%.3f,"
                "\"session_count\":%u}",
                day.tm_year + 1900, day.tm_mon + 1, day.tm_mday,
                active, sessions);
    }
}

static int
write_summary(uint64_t now_ms) {
    const char *temporary = TRACKER_SUMMARY_PATH ".tmp";
    FILE *file = fopen(temporary, "w");
    uint64_t total_active = 0, total_paused = 0;
    double today_active[MAX_GAMES] = {0};
    unsigned today_sessions[MAX_GAMES] = {0};
    char generated[32], generated_local[32];
    unsigned completed_count = 0;
    unsigned current_valid = state.current_session >= 0
        && (uint32_t)state.current_session < state.session_count;
    if(!file) return -1;
    iso_time(generated, now_ms);
    local_iso_time(generated_local, now_ms);
    for(uint32_t i = 0; i < state.game_count; i++) {
        total_active += state.games[i].active_ms;
        total_paused += state.games[i].paused_ms;
        if(completed_ms(state.games[i].title_id)) completed_count++;
    }
    {
        struct tm now_tm;
        time_t now_local = (time_t)(now_ms / 1000
                                    + timezone_offset_minutes * 60);
        time_t today_start_local;
        uint64_t today_start;
        uint64_t today_end;
        tracker_local_tm(now_ms, &now_tm);
        today_start_local = now_local
            - (now_tm.tm_hour * 3600 + now_tm.tm_min * 60 + now_tm.tm_sec);
        today_start = (uint64_t)
            (today_start_local - timezone_offset_minutes * 60) * 1000;
        today_end = today_start + 86400ULL * 1000;
        for(uint32_t i = 0; i < state.session_count; i++) {
            tracker_session_t *session = &state.sessions[i];
            uint64_t elapsed = session->ended_ms > session->started_ms
                ? session->ended_ms - session->started_ms : 0;
            uint64_t overlap = overlap_ms(
                session->started_ms, session->ended_ms,
                today_start, today_end);
            if(elapsed && overlap && session->game_index < state.game_count) {
                today_active[session->game_index] +=
                    session->active_ms / 1000.0 * (double)overlap / elapsed;
                today_sessions[session->game_index]++;
            }
        }
    }
    fprintf(file,
            "{\n  \"generated_at\":\"%s\","
            "\"generated_at_local\":\"%s\",\"timezone\":\"",
            generated, generated_local);
    json_escape(file, timezone_name);
    fprintf(file,
            "\",\"timezone_offset_minutes\":%d,"
            "\"timezone_source\":\"%s\","
            "\"system\":{\"tracker_version\":\"" TRACKER_VERSION "\","
            "\"probe_version\":\"" PROBE_VERSION "\",\"firmware\":\"%s\","
            "\"console_ip\":\"%s\"},"
            "\"totals\":{\"games\":%u,"
            "\"sessions\":%u,\"active_seconds\":%.3f,"
            "\"paused_seconds\":%.3f,\"home_share_percent\":%.1f,"
            "\"completed_games\":%u},\n  \"games\":[",
            timezone_offset_minutes, timezone_source, firmware_version,
            console_ip,
            state.game_count, state.session_count,
            total_active / 1000.0, total_paused / 1000.0,
            total_active + total_paused
                ? total_paused * 100.0 / (total_active + total_paused) : 0.0,
            completed_count);
    for(uint32_t i = 0; i < state.game_count; i++) {
        if(i) fputc(',', file);
        fputs("\n    ", file);
        write_game(file, i, today_active[i], today_sessions[i]);
    }
    fputs("\n  ],\n  \"sessions\":[", file);
    for(uint32_t n = 0; n < state.session_count; n++) {
        uint32_t index = state.session_count - 1 - n;
        if(n) fputc(',', file);
        fputs("\n    ", file);
        write_session(file, &state.sessions[index]);
    }
    fputs("\n  ],\n  \"days\":[\n    ", file);
    write_days(file, now_ms);
    fputs("\n  ],\n  \"periods\":{\"today\":", file);
    write_period(file, now_ms, 1, 1);
    fputs(",\"week\":", file); write_period(file, now_ms, 7, 0);
    fputs(",\"month\":", file); write_period(file, now_ms, 30, 0);
    fputs("},\n  \"health\":{\"median_load_seconds\":null,"
          "\"successful_launches\":", file);
    fprintf(file, "%u,\"failed_launches\":0,\"launch_success_percent\":100,"
            "\"crash_count\":0,\"current_activity\":", state.session_count);
    if(current_valid) {
        tracker_session_t *session = &state.sessions[state.current_session];
        tracker_game_t *game = &state.games[session->game_index];
        char since[32], since_local[32];
        iso_time(since, session->started_ms);
        local_iso_time(since_local, session->started_ms);
        fputs("{\"title_id\":\"", file); json_escape(file, game->title_id);
        fputs("\",\"name\":\"", file); json_escape(file, display_name(game));
        fprintf(file,
                "\",\"state\":\"%s\",\"since\":\"%s\","
                "\"since_local\":\"%s\"}",
                session->is_active ? "active" : "paused", since, since_local);
    } else {
        fputs("null", file);
    }
    fputs(",\"warnings\":[],\"diagnostics\":[]}\n}", file);
    fputc('\n', file);
    if(fflush(file) != 0 || fsync(fileno(file)) != 0) {
        fclose(file);
        unlink(temporary);
        return -1;
    }
    fclose(file);
    if(rename(temporary, TRACKER_SUMMARY_PATH) != 0) {
        unlink(temporary);
        return -1;
    }
    chmod(TRACKER_SUMMARY_PATH, 0644);
    return 0;
}

static void
persist(uint64_t now_ms) {
    state.last_write_ms = now_ms;
    write_state();
    write_summary(now_ms);
}

static const char *backup_names[BACKUP_FILE_COUNT] = {
    "tracker-state.bin", "completed-state.bin",
    "config.json", "diagnostics-state.bin"
};

static const char *backup_sources[BACKUP_FILE_COUNT] = {
    TRACKER_STATE_PATH, TRACKER_COMPLETED_PATH,
    TRACKER_CONFIG_PATH, TRACKER_DIAGNOSTICS_PATH
};

static int
valid_backup_id(const char *id) {
    size_t length;
    if(!id || !(length = strlen(id)) || length >= 64) return 0;
    for(size_t i = 0; i < length; i++) {
        char c = id[i];
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
             || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static int
file_crc(const char *path, uint64_t *size, uint32_t *crc) {
    unsigned char buffer[8192];
    uint32_t value = 0xffffffffu;
    uint64_t total = 0;
    FILE *file = fopen(path, "rb");
    size_t count;
    if(!file) return -1;
    while((count = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        total += count;
        for(size_t i = 0; i < count; i++) {
            value ^= buffer[i];
            for(int bit = 0; bit < 8; bit++) {
                value = (value >> 1)
                    ^ (0xedb88320u & (0u - (value & 1u)));
            }
        }
    }
    if(ferror(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    *size = total;
    *crc = ~value;
    return 0;
}

static int
copy_file(const char *source, const char *destination) {
    char buffer[8192];
    int input = open(source, O_RDONLY);
    int output;
    ssize_t count;
    if(input < 0) return -1;
    output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(output < 0) {
        close(input);
        return -1;
    }
    while((count = read(input, buffer, sizeof(buffer))) > 0) {
        ssize_t written = 0;
        while(written < count) {
            ssize_t result = write(output, buffer + written,
                                   (size_t)(count - written));
            if(result <= 0) {
                close(input);
                close(output);
                unlink(destination);
                return -1;
            }
            written += result;
        }
    }
    if(count < 0 || fsync(output) != 0) {
        close(input);
        close(output);
        unlink(destination);
        return -1;
    }
    close(input);
    close(output);
    chmod(destination, 0644);
    return 0;
}

static void
remove_backup_files(const char *directory) {
    char path[512];
    for(int i = 0; i < BACKUP_FILE_COUNT; i++) {
        snprintf(path, sizeof(path), "%s/%s", directory, backup_names[i]);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/backup.meta", directory);
    unlink(path);
    snprintf(path, sizeof(path), "%s/manifest.json", directory);
    unlink(path);
    rmdir(directory);
}

static int
load_backup_meta(const char *id, backup_meta_t *meta, int verify) {
    char directory[512];
    char path[512];
    FILE *file;
    if(!valid_backup_id(id)) return -1;
    snprintf(directory, sizeof(directory), "%s/%s", TRACKER_BACKUPS_DIR, id);
    snprintf(path, sizeof(path), "%s/backup.meta", directory);
    file = fopen(path, "rb");
    if(!file) return -1;
    if(fread(meta, sizeof(*meta), 1, file) != 1
       || fgetc(file) != EOF || meta->magic != BACKUP_MAGIC
       || meta->version != BACKUP_VERSION) {
        fclose(file);
        return -1;
    }
    fclose(file);
    if(verify) {
        for(int i = 0; i < BACKUP_FILE_COUNT; i++) {
            uint64_t size;
            uint32_t crc;
            if(!meta->files[i].present) return -1;
            snprintf(path, sizeof(path), "%s/%s",
                     directory, backup_names[i]);
            if(file_crc(path, &size, &crc) != 0
               || size != meta->files[i].size
               || crc != meta->files[i].crc32) return -1;
        }
    }
    return 0;
}

static int
write_backups_index_unlocked(void) {
    const char *temporary = TRACKER_BACKUPS_INDEX_PATH ".tmp";
    DIR *directory;
    struct dirent *entry;
    FILE *file;
    int first = 1;
    mkdir(TRACKER_BACKUPS_DIR, 0755);
    directory = opendir(TRACKER_BACKUPS_DIR);
    if(!directory) return -1;
    file = fopen(temporary, "w");
    if(!file) {
        closedir(directory);
        return -1;
    }
    fputs("{\"backups\":[", file);
    while((entry = readdir(directory)) != NULL) {
        backup_meta_t meta;
        char created[32];
        uint64_t total_size = 0;
        int valid;
        if(entry->d_name[0] == '.'
           || !valid_backup_id(entry->d_name)) continue;
        valid = load_backup_meta(entry->d_name, &meta, 1) == 0;
        if(!valid && load_backup_meta(entry->d_name, &meta, 0) != 0) {
            continue;
        }
        local_iso_time(created, meta.created_ms);
        for(int i = 0; i < BACKUP_FILE_COUNT; i++) {
            total_size += meta.files[i].size;
        }
        if(!first) fputc(',', file);
        first = 0;
        fputs("{\"id\":\"", file);
        json_escape(file, entry->d_name);
        fprintf(file,
                "\",\"kind\":\"%s\",\"created_at\":\"%s\","
                "\"games\":%u,\"sessions\":%u,\"active_seconds\":%.3f,"
                "\"size_bytes\":%llu,\"valid\":%s}",
                strncmp(entry->d_name, "before-restore-", 15) == 0
                    ? "before_restore" : "manual",
                created, meta.game_count, meta.session_count,
                meta.active_ms / 1000.0,
                (unsigned long long)total_size,
                valid ? "true" : "false");
    }
    fputs("]}\n", file);
    closedir(directory);
    if(fflush(file) != 0 || fsync(fileno(file)) != 0) {
        fclose(file);
        unlink(temporary);
        return -1;
    }
    fclose(file);
    if(rename(temporary, TRACKER_BACKUPS_INDEX_PATH) != 0) {
        unlink(temporary);
        return -1;
    }
    chmod(TRACKER_BACKUPS_INDEX_PATH, 0644);
    return 0;
}

static int
create_backup_unlocked(uint64_t now_ms, const char *prefix,
                       char *output_id, size_t output_size) {
    char id[64], temporary[512], destination[512], path[512];
    char stamp[32], created[32];
    backup_meta_t meta;
    FILE *file;
    uint64_t active_ms = 0;
    time_t seconds = (time_t)(now_ms / 1000
                              + timezone_offset_minutes * 60);
    struct tm local;
    gmtime_r(&seconds, &local);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    snprintf(id, sizeof(id), "%s%s", prefix, stamp);
    snprintf(temporary, sizeof(temporary), "%s/.creating-%s",
             TRACKER_BACKUPS_DIR, id);
    snprintf(destination, sizeof(destination), "%s/%s",
             TRACKER_BACKUPS_DIR, id);
    mkdir(TRACKER_BACKUPS_DIR, 0755);
    remove_backup_files(temporary);
    if(access(destination, F_OK) == 0 || mkdir(temporary, 0755) != 0) {
        return -1;
    }
    memset(&meta, 0, sizeof(meta));
    meta.magic = BACKUP_MAGIC;
    meta.version = BACKUP_VERSION;
    meta.created_ms = now_ms;
    meta.game_count = state.game_count;
    meta.session_count = state.session_count;
    for(uint32_t i = 0; i < state.game_count; i++) {
        active_ms += state.games[i].active_ms;
    }
    meta.active_ms = active_ms;
    for(int i = 0; i < BACKUP_FILE_COUNT; i++) {
        snprintf(path, sizeof(path), "%s/%s", temporary, backup_names[i]);
        if(copy_file(backup_sources[i], path) != 0
           || file_crc(path, &meta.files[i].size,
                       &meta.files[i].crc32) != 0) {
            remove_backup_files(temporary);
            return -1;
        }
        meta.files[i].present = 1;
    }
    snprintf(path, sizeof(path), "%s/backup.meta", temporary);
    file = fopen(path, "wb");
    if(!file || fwrite(&meta, sizeof(meta), 1, file) != 1
       || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        if(file) fclose(file);
        remove_backup_files(temporary);
        return -1;
    }
    fclose(file);
    local_iso_time(created, now_ms);
    snprintf(path, sizeof(path), "%s/manifest.json", temporary);
    file = fopen(path, "w");
    if(!file) {
        remove_backup_files(temporary);
        return -1;
    }
    fprintf(file,
            "{\n  \"version\": 1,\n  \"created_at\": \"%s\",\n"
            "  \"games\": %u,\n  \"sessions\": %u,\n"
            "  \"active_seconds\": %.3f,\n  \"integrity\": \"crc32\"\n}\n",
            created, meta.game_count, meta.session_count,
            meta.active_ms / 1000.0);
    if(fflush(file) != 0 || fsync(fileno(file)) != 0) {
        fclose(file);
        remove_backup_files(temporary);
        return -1;
    }
    fclose(file);
    if(rename(temporary, destination) != 0
       || load_backup_meta(id, &meta, 1) != 0) {
        remove_backup_files(temporary);
        remove_backup_files(destination);
        return -1;
    }
    if(output_id && output_size) {
        snprintf(output_id, output_size, "%s", id);
    }
    return write_backups_index_unlocked();
}

int
tracker_backup_refresh_index(void) {
    int result;
    pthread_mutex_lock(&state_mutex);
    result = write_backups_index_unlocked();
    pthread_mutex_unlock(&state_mutex);
    return result;
}

int
tracker_backup_create(uint64_t now_ms, char *backup_id,
                      unsigned backup_id_size) {
    int result;
    pthread_mutex_lock(&state_mutex);
    checkpoint(now_ms);
    persist(now_ms);
    if(write_completed_state() != 0 || write_config_file() != 0
       || write_diagnostics_state() != 0) {
        result = -1;
    } else {
        result = create_backup_unlocked(
            now_ms, "backup-", backup_id, backup_id_size);
    }
    pthread_mutex_unlock(&state_mutex);
    return result;
}

int
tracker_backup_restore(const char *id, uint64_t now_ms) {
    char directory[512], source[512], temporary[512];
    backup_meta_t meta;
    static tracker_state_t restored;
    uint64_t generation;
    int legacy;
    int result = -1;
    pthread_mutex_lock(&state_mutex);
    if(load_backup_meta(id, &meta, 1) != 0) goto done;
    snprintf(directory, sizeof(directory), "%s/%s", TRACKER_BACKUPS_DIR, id);
    snprintf(source, sizeof(source), "%s/%s", directory, backup_names[0]);
    if(load_state_file(source, &restored, &generation, &legacy) != 0) goto done;
    checkpoint(now_ms);
    persist(now_ms);
    if(write_completed_state() != 0 || write_config_file() != 0
       || write_diagnostics_state() != 0
       || create_backup_unlocked(now_ms, "before-restore-", NULL, 0) != 0) {
        goto done;
    }
    for(int i = 0; i < BACKUP_FILE_COUNT; i++) {
        snprintf(source, sizeof(source), "%s/%s",
                 directory, backup_names[i]);
        snprintf(temporary, sizeof(temporary), "%s.restore.tmp",
                 backup_sources[i]);
        if(copy_file(source, temporary) != 0) goto cleanup;
    }
    for(int i = 0; i < BACKUP_FILE_COUNT; i++) {
        snprintf(temporary, sizeof(temporary), "%s.restore.tmp",
                 backup_sources[i]);
        if(rename(temporary, backup_sources[i]) != 0) goto cleanup;
    }
    state = restored;
    state_generation = generation;
    if(state.current_session >= 0
       && (uint32_t)state.current_session < state.session_count) {
        state.sessions[state.current_session].is_active = 0;
        state.sessions[state.current_session].is_open = 0;
        state.sessions[state.current_session].ended_ms = now_ms;
        state.sessions[state.current_session].mark_ms = now_ms;
        state.current_session = -1;
    }
    load_completed_state();
    load_config(now_ms);
    load_diagnostics_state();
    persist(now_ms);
    write_backups_index_unlocked();
    result = 0;
    goto done;
cleanup:
    for(int i = 0; i < BACKUP_FILE_COUNT; i++) {
        snprintf(temporary, sizeof(temporary), "%s.restore.tmp",
                 backup_sources[i]);
        unlink(temporary);
    }
done:
    pthread_mutex_unlock(&state_mutex);
    return result;
}

int
tracker_backup_delete(const char *id) {
    char directory[512];
    backup_meta_t meta;
    int result = -1;
    pthread_mutex_lock(&state_mutex);
    if(load_backup_meta(id, &meta, 0) == 0) {
        snprintf(directory, sizeof(directory), "%s/%s",
                 TRACKER_BACKUPS_DIR, id);
        remove_backup_files(directory);
        result = access(directory, F_OK) == 0
            ? -1 : write_backups_index_unlocked();
    }
    pthread_mutex_unlock(&state_mutex);
    return result;
}

static void
quarantine_state_file(const char *path, const char *label,
                      uint64_t now_ms) {
    char destination[256];
    snprintf(destination, sizeof(destination),
             TRACKER_DATA_DIR "/tracker-state.%s.corrupt-%llu.bin",
             label, (unsigned long long)(now_ms / 1000));
    rename(path, destination);
}

int
tracker_init(uint64_t now_ms) {
    static tracker_state_t loaded;
    uint64_t loaded_generation = 0;
    int legacy = 0;
    int main_existed = access(TRACKER_STATE_PATH, F_OK) == 0;
    int previous_existed = access(TRACKER_PREVIOUS_STATE_PATH, F_OK) == 0;
    int recovery = 0;
    memset(&state, 0, sizeof(state));
    state_generation = 0;
    detect_console_ip();
    detect_firmware();
    load_config(now_ms);
    state.current_session = -1;
    load_completed_state();
    load_diagnostics_state();
    if(load_state_file(TRACKER_STATE_PATH, &loaded, &loaded_generation,
                       &legacy) == 0) {
        state = loaded;
        state_generation = loaded_generation;
    } else {
        if(main_existed) {
            quarantine_state_file(TRACKER_STATE_PATH, "main", now_ms);
        }
        if(load_state_file(TRACKER_PREVIOUS_STATE_PATH, &loaded,
                           &loaded_generation, &legacy) == 0) {
            state = loaded;
            state_generation = loaded_generation;
            recovery = 1;
        } else if(main_existed || previous_existed) {
            if(previous_existed) {
                quarantine_state_file(
                    TRACKER_PREVIOUS_STATE_PATH, "previous", now_ms);
            }
            recovery = 2;
        }
    }
    if(state.current_session >= 0
       && (uint32_t)state.current_session < state.session_count) {
        state.sessions[state.current_session].is_active = 0;
        state.sessions[state.current_session].mark_ms = now_ms;
        state.sessions[state.current_session].ended_ms = now_ms;
    }
    state.magic = STATE_MAGIC;
    state.version = STATE_VERSION;
    state.last_write_ms = now_ms;
    persist(now_ms);
    write_backups_index_unlocked();
    if(recovery == 1) {
        tracker_diagnostic(
            "warning", "state_recovered", NULL,
            "tracker-state.bin failed validation; restored previous state",
            now_ms);
    } else if(recovery == 2) {
        tracker_diagnostic(
            "critical", "state_reset", NULL,
            "state files failed validation; started with empty state",
            now_ms);
    }
    return 0;
}

static void
tracker_event_unlocked(const char *event, const char *title_id,
                       const char *title_name, uint64_t now_ms) {
    int index;
    tracker_session_t *session;
    tracker_game_t *game;
    if(!event) return;
    if(strcmp(event, "metadata") == 0) {
        index = game_index(title_id, 1);
        if(index >= 0 && title_name && title_name[0]) {
            snprintf(state.games[index].name,
                     sizeof(state.games[index].name), "%s", title_name);
            persist(now_ms);
        }
        return;
    }
    if(strcmp(event, "foreground") == 0) {
        index = game_index(title_id, 1);
        if(index < 0) return;
        if(state.current_session >= 0) {
            session = &state.sessions[state.current_session];
            if(session->game_index == (uint16_t)index && session->is_open) {
                checkpoint(now_ms);
                session->is_active = 1;
                session->mark_ms = now_ms;
                persist(now_ms);
                return;
            }
            close_current(now_ms);
        }
        if(state.session_count == MAX_SESSIONS) {
            memmove(&state.sessions[0], &state.sessions[1],
                    sizeof(state.sessions[0]) * (MAX_SESSIONS - 1));
            state.session_count--;
        }
        state.current_session = (int32_t)state.session_count++;
        session = &state.sessions[state.current_session];
        memset(session, 0, sizeof(*session));
        session->game_index = (uint16_t)index;
        session->is_open = 1;
        session->is_active = 1;
        session->started_ms = session->ended_ms = session->mark_ms = now_ms;
        game = &state.games[index];
        game->session_count++;
        if(!game->first_played_ms) game->first_played_ms = now_ms;
        game->last_played_ms = now_ms;
        persist(now_ms);
        return;
    }
    if(state.current_session < 0) return;
    session = &state.sessions[state.current_session];
    game = &state.games[session->game_index];
    if(title_id && strcmp(game->title_id, title_id) != 0) return;
    if(strcmp(event, "background") == 0) {
        checkpoint(now_ms);
        session->is_active = 0;
        session->mark_ms = now_ms;
        persist(now_ms);
    } else if(strcmp(event, "exit") == 0) {
        close_current(now_ms);
        persist(now_ms);
    }
}

void
tracker_event(const char *event, const char *title_id,
              const char *title_name, uint64_t now_ms) {
    pthread_mutex_lock(&state_mutex);
    tracker_event_unlocked(event, title_id, title_name, now_ms);
    pthread_mutex_unlock(&state_mutex);
}

void
tracker_tick(uint64_t now_ms) {
    pthread_mutex_lock(&state_mutex);
    if(state.last_write_ms == 0
       || now_ms >= state.last_write_ms + WRITE_INTERVAL_MS) {
        checkpoint(now_ms);
        persist(now_ms);
    }
    pthread_mutex_unlock(&state_mutex);
}

void
tracker_shutdown(uint64_t now_ms) {
    pthread_mutex_lock(&state_mutex);
    checkpoint(now_ms);
    persist(now_ms);
    pthread_mutex_unlock(&state_mutex);
}

int
tracker_set_completed(const char *title_id, int completed,
                      uint64_t now_ms) {
    uint32_t index;
    int result = -1;
    pthread_mutex_lock(&state_mutex);
    if(game_index(title_id, 0) < 0) goto done;
    for(index = 0; index < completed_state.count; index++) {
        if(strcmp(completed_state.games[index].title_id, title_id) == 0) break;
    }
    if(completed) {
        if(index == completed_state.count) {
            if(completed_state.count >= MAX_GAMES) goto done;
            memset(&completed_state.games[index], 0,
                   sizeof(completed_state.games[index]));
            snprintf(completed_state.games[index].title_id,
                     sizeof(completed_state.games[index].title_id),
                     "%s", title_id);
            completed_state.count++;
        }
        completed_state.games[index].completed_ms = now_ms;
    } else if(index < completed_state.count) {
        if(index + 1 < completed_state.count) {
            memmove(&completed_state.games[index],
                    &completed_state.games[index + 1],
                    sizeof(completed_state.games[0])
                        * (completed_state.count - index - 1));
        }
        completed_state.count--;
        memset(&completed_state.games[completed_state.count], 0,
               sizeof(completed_state.games[0]));
    }
    if(write_completed_state() == 0) result = write_summary(now_ms);
done:
    pthread_mutex_unlock(&state_mutex);
    return result;
}

int
tracker_set_config(int timezone_offset, const char *timezone,
                   const char *firmware, uint64_t now_ms) {
    int result = -1;
    if(timezone_offset < -720 || timezone_offset > 840
       || !valid_config_text(timezone, sizeof(timezone_name))
       || !valid_config_text(firmware, sizeof(firmware_version))) {
        return -1;
    }
    pthread_mutex_lock(&state_mutex);
    timezone_offset_minutes = timezone_offset;
    snprintf(timezone_name, sizeof(timezone_name), "%s", timezone);
    snprintf(firmware_version, sizeof(firmware_version), "%s", firmware);
    snprintf(timezone_source, sizeof(timezone_source), "config");
    if(write_config_file() == 0) result = write_summary(now_ms);
    pthread_mutex_unlock(&state_mutex);
    return result;
}

void
tracker_diagnostic(const char *level, const char *type,
                   const char *title_id, const char *message,
                   uint64_t now_ms) {
    diagnostic_entry_t *entry = NULL;
    uint32_t index;
    if(!level || !type || !message || !message[0]) return;
    pthread_mutex_lock(&state_mutex);
    if(diagnostics_state.count) {
        entry = &diagnostics_state.entries[diagnostics_state.count - 1];
        if(strcmp(entry->type, type) != 0
           || strcmp(entry->title_id, title_id ? title_id : "") != 0
           || strcmp(entry->message, message) != 0
           || now_ms > entry->occurred_ms + DIAGNOSTIC_DEDUP_MS) {
            entry = NULL;
        }
    }
    if(entry) {
        entry->occurred_ms = now_ms;
        entry->count++;
    } else {
        if(diagnostics_state.count == MAX_DIAGNOSTICS) {
            memmove(&diagnostics_state.entries[0],
                    &diagnostics_state.entries[1],
                    sizeof(diagnostics_state.entries[0])
                        * (MAX_DIAGNOSTICS - 1));
            diagnostics_state.count--;
        }
        index = diagnostics_state.count++;
        entry = &diagnostics_state.entries[index];
        memset(entry, 0, sizeof(*entry));
        snprintf(entry->level, sizeof(entry->level), "%s", level);
        snprintf(entry->type, sizeof(entry->type), "%s", type);
        if(title_id) {
            snprintf(entry->title_id, sizeof(entry->title_id),
                     "%s", title_id);
        }
        snprintf(entry->message, sizeof(entry->message), "%s", message);
        entry->occurred_ms = now_ms;
        entry->count = 1;
    }
    write_diagnostics_state();
    write_summary(now_ms);
    pthread_mutex_unlock(&state_mutex);
}
