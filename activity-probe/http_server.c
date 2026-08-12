#include "http_server.h"
#include "offline_update.h"
#include "tracker.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static pthread_t server_thread;
static int server_fd = -1;
static volatile int server_running;

static const char setup_html[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Playlog setup</title><style>"
    "html,body{margin:0;min-height:100%;background:#030916;color:#f5f8ff;"
    "font-family:Arial,sans-serif}body{display:flex;align-items:center;"
    "justify-content:center}.card{width:760px;max-width:84vw;padding:42px;"
    "border:1px solid #356db4;border-radius:24px;background:#0a1b37}"
    "h1{font-size:38px;margin:0 0 12px}p{color:#a9bddb;line-height:1.5}"
    ".choices{display:flex;gap:16px;margin-top:28px}button{flex:1;padding:20px;"
    "border:1px solid #3f8eff;border-radius:14px;background:#102c58;"
    "color:white;font-size:20px}button:focus{outline:4px solid #72b5ff}"
    "#status{min-height:28px;margin-top:24px;color:#ffd774;white-space:pre-line}"
    "</style></head>"
    "<body><main class=card><h1>Playlog</h1>"
    "<p id=intro>Choose how payloads start on this console.</p>"
    "<div class=choices><button onclick=\"install('etahen')\">etaHEN</button>"
    "<button onclick=\"install('autoloader')\">ShadowMount+ / PLK</button></div>"
    "<div id=status></div></main><script>"
    "function install(mode){var s=document.getElementById('status');"
    "s.textContent='Installing Playlog...';fetch('/api/setup/install?mode='+mode,"
    "{method:'POST',cache:'no-store'}).then(function(r){return r.json()"
    ".then(function(j){if(!r.ok||!j.ok)throw Error(j.error||r.status);return j})})"
    ".then(function(j){s.textContent=j.manual_autoload_required?"
    "'Playlog files were copied, but autoload.txt was left unchanged because it is missing or contains no payload chain. Payload Manager: keep the file missing (or remove an empty file) and enable Playlog in PLK. Manual autoloader: add the jailbreak payloads first, then Playlog.elf.':"
    "'Installed. Restart etaHEN or the console.'})"
    ".catch(function(e){s.textContent='Installation failed: '+e.message})}"
    "</script></body></html>";

static int
send_all(int fd, const void *buffer, size_t length) {
    const char *cursor = buffer;
    while(length) {
        ssize_t sent = send(fd, cursor, length, 0);
        if(sent <= 0) return -1;
        cursor += sent;
        length -= (size_t)sent;
    }
    return 0;
}

static void
send_text(int client, int status, const char *status_text,
          const char *content_type, const char *body) {
    char header[512];
    size_t length = strlen(body);
    int header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
        "Content-Length: %zu\r\nCache-Control: no-store\r\n"
        "Connection: close\r\n\r\n",
        status, status_text, content_type, length);
    if(header_length > 0) {
        send_all(client, header, (size_t)header_length);
        send_all(client, body, length);
    }
}

static void
send_file(int client, const char *path, const char *content_type) {
    struct stat info;
    char header[512];
    char buffer[8192];
    int file;
    ssize_t count;
    int header_length;
    if(stat(path, &info) != 0 || info.st_size < 0
       || (file = open(path, O_RDONLY)) < 0) {
        send_text(client, 404, "Not Found", "text/plain; charset=utf-8",
                  "Playlog file not found\n");
        return;
    }
    header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
        "Content-Length: %lld\r\nCache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
        content_type, (long long)info.st_size);
    if(header_length > 0
       && send_all(client, header, (size_t)header_length) == 0) {
        while((count = read(file, buffer, sizeof(buffer))) > 0) {
            if(send_all(client, buffer, (size_t)count) != 0) break;
        }
    }
    close(file);
}

static int
hex_value(char value) {
    if(value >= '0' && value <= '9') return value - '0';
    if(value >= 'a' && value <= 'f') return value - 'a' + 10;
    if(value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int
query_value(const char *query, const char *key,
            char *output, size_t output_size) {
    size_t key_length = strlen(key);
    const char *cursor = query;
    size_t written = 0;
    while(cursor && *cursor) {
        if(strncmp(cursor, key, key_length) == 0
           && cursor[key_length] == '=') {
            cursor += key_length + 1;
            while(*cursor && *cursor != '&') {
                char value = *cursor++;
                if(value == '%' && cursor[0] && cursor[1]) {
                    int high = hex_value(cursor[0]);
                    int low = hex_value(cursor[1]);
                    if(high < 0 || low < 0) return -1;
                    value = (char)((high << 4) | low);
                    cursor += 2;
                } else if(value == '+') {
                    value = ' ';
                }
                if(written + 1 >= output_size) return -1;
                output[written++] = value;
            }
            output[written] = '\0';
            return written ? 0 : -1;
        }
        cursor = strchr(cursor, '&');
        if(cursor) cursor++;
    }
    return -1;
}

static int
address_is_local_interface(const struct sockaddr_storage *peer) {
    struct ifaddrs *interfaces = NULL;
    int result = 0;
    if(!peer || getifaddrs(&interfaces) != 0) return 0;
    for(struct ifaddrs *item = interfaces; item; item = item->ifa_next) {
        if(!item->ifa_addr || item->ifa_addr->sa_family != peer->ss_family) {
            continue;
        }
        if(peer->ss_family == AF_INET
           && memcmp(&((const struct sockaddr_in *)item->ifa_addr)->sin_addr,
                     &((const struct sockaddr_in *)peer)->sin_addr,
                     sizeof(struct in_addr)) == 0) {
            result = 1;
            break;
        }
        if(peer->ss_family == AF_INET6
           && memcmp(&((const struct sockaddr_in6 *)item->ifa_addr)->sin6_addr,
                     &((const struct sockaddr_in6 *)peer)->sin6_addr,
                     sizeof(struct in6_addr)) == 0) {
            result = 1;
            break;
        }
    }
    freeifaddrs(interfaces);
    return result;
}

static void
handle_client(int client, int local_client,
              const struct sockaddr_storage *peer) {
    char request[2048];
    ssize_t length = recv(client, request, sizeof(request) - 1, 0);
    char method[8] = "";
    char path[256] = "";
    char title_id[10] = "";
    char offset_text[16] = "";
    char timezone[64] = "";
    char firmware[16] = "";
    char backup_id[64] = "";
    int completed = -1;
    if(length <= 0) return;
    request[length] = '\0';
    /* Do not trust the HTTP Host header for authorization: a LAN client can
       spoof localhost.  The peer address is the only source of local access. */
    local_client = local_client || address_is_local_interface(peer);
    if(sscanf(request, "%7s %255s", method, path) != 2) {
        send_text(client, 400, "Bad Request", "text/plain",
                  "Bad request\n");
        return;
    }
    /* The LAN listener is intentionally read-only.  Keep every mutating
       operation restricted to the console's own browser/UI. */
    if(!local_client && strcmp(method, "POST") == 0) {
        send_text(client, 403, "Forbidden",
                  "application/json; charset=utf-8",
                  "{\"ok\":false,\"error\":\"LAN access is read-only\"}\n");
        return;
    }
    if(strcmp(method, "POST") == 0
       && strncmp(path, "/api/setup/install?", 19) == 0) {
        char mode[16] = "";
        char response[OFFLINE_UPDATE_STATUS_SIZE];
        int result;
        if(query_value(path + 19, "mode", mode, sizeof(mode)) != 0) {
            send_text(client, 400, "Bad Request",
                      "application/json; charset=utf-8",
                      "{\"ok\":false,\"error\":\"mode is required\"}\n");
            return;
        }
        result = offline_setup_install(mode, response, sizeof(response));
        send_text(client, result == 0 ? 200 : 500,
                  result == 0 ? "OK" : "Internal Server Error",
                  "application/json; charset=utf-8", response);
        return;
    }
    if(strcmp(method, "POST") == 0
       && strcmp(path, "/api/update/apply") == 0) {
        char response[OFFLINE_UPDATE_STATUS_SIZE];
        int result = offline_update_apply(response, sizeof(response));
        send_text(client, result == 0 ? 200 : 500,
                  result == 0 ? "OK" : "Internal Server Error",
                  "application/json; charset=utf-8", response);
        return;
    }
    if(strcmp(method, "POST") == 0
       && sscanf(path,
                 "/api/completed?title_id=%9[A-Za-z0-9]&completed=%d",
                 title_id, &completed) == 2) {
        struct timespec now;
        uint64_t now_ms;
        if((completed != 0 && completed != 1)
           || clock_gettime(CLOCK_REALTIME, &now) != 0) {
            send_text(client, 400, "Bad Request",
                      "application/json; charset=utf-8",
                      "{\"ok\":false,\"error\":\"invalid request\"}\n");
            return;
        }
        now_ms = (uint64_t)now.tv_sec * 1000
            + (uint64_t)now.tv_nsec / 1000000;
        if(tracker_set_completed(title_id, completed, now_ms) == 0) {
            send_text(client, 200, "OK",
                      "application/json; charset=utf-8",
                      "{\"ok\":true}\n");
        } else {
            send_text(client, 404, "Not Found",
                      "application/json; charset=utf-8",
                      "{\"ok\":false,\"error\":\"game not found\"}\n");
        }
        return;
    }
    if(strcmp(method, "POST") == 0
       && strncmp(path, "/api/config?", 12) == 0
       && query_value(path + 12, "timezone_offset_minutes",
                      offset_text, sizeof(offset_text)) == 0
       && query_value(path + 12, "timezone_name",
                      timezone, sizeof(timezone)) == 0
       && query_value(path + 12, "firmware",
                      firmware, sizeof(firmware)) == 0) {
        char *end = NULL;
        long offset = strtol(offset_text, &end, 10);
        struct timespec now;
        uint64_t now_ms;
        if(!end || *end || clock_gettime(CLOCK_REALTIME, &now) != 0
           || tracker_set_config((int)offset, timezone, firmware,
                                 (uint64_t)now.tv_sec * 1000
                                     + (uint64_t)now.tv_nsec / 1000000) != 0) {
            send_text(client, 400, "Bad Request",
                      "application/json; charset=utf-8",
                      "{\"ok\":false,\"error\":\"invalid config\"}\n");
            return;
        }
        now_ms = (uint64_t)now.tv_sec * 1000
            + (uint64_t)now.tv_nsec / 1000000;
        (void)now_ms;
        send_text(client, 200, "OK",
                  "application/json; charset=utf-8",
                  "{\"ok\":true}\n");
        return;
    }
    if(strcmp(method, "POST") == 0
       && strcmp(path, "/api/backups/create") == 0) {
        struct timespec now;
        char response[160];
        if(clock_gettime(CLOCK_REALTIME, &now) != 0
           || tracker_backup_create(
               (uint64_t)now.tv_sec * 1000
                   + (uint64_t)now.tv_nsec / 1000000,
               backup_id, sizeof(backup_id)) != 0) {
            send_text(client, 500, "Internal Server Error",
                      "application/json; charset=utf-8",
                      "{\"ok\":false,\"error\":\"backup failed\"}\n");
            return;
        }
        snprintf(response, sizeof(response),
                 "{\"ok\":true,\"id\":\"%s\"}\n", backup_id);
        send_text(client, 200, "OK",
                  "application/json; charset=utf-8", response);
        return;
    }
    if(strcmp(method, "POST") == 0
       && strncmp(path, "/api/backups/restore?", 21) == 0
       && query_value(path + 21, "id", backup_id,
                      sizeof(backup_id)) == 0) {
        struct timespec now;
        if(clock_gettime(CLOCK_REALTIME, &now) != 0
           || tracker_backup_restore(
               backup_id, (uint64_t)now.tv_sec * 1000
                   + (uint64_t)now.tv_nsec / 1000000) != 0) {
            send_text(client, 400, "Bad Request",
                      "application/json; charset=utf-8",
                      "{\"ok\":false,\"error\":\"restore failed\"}\n");
            return;
        }
        send_text(client, 200, "OK",
                  "application/json; charset=utf-8",
                  "{\"ok\":true}\n");
        return;
    }
    if(strcmp(method, "POST") == 0
       && strncmp(path, "/api/backups/delete?", 20) == 0
       && query_value(path + 20, "id", backup_id,
                      sizeof(backup_id)) == 0) {
        if(tracker_backup_delete(backup_id) != 0) {
            send_text(client, 400, "Bad Request",
                      "application/json; charset=utf-8",
                      "{\"ok\":false,\"error\":\"delete failed\"}\n");
            return;
        }
        send_text(client, 200, "OK",
                  "application/json; charset=utf-8",
                  "{\"ok\":true}\n");
        return;
    }
    if(strcmp(method, "GET") != 0) {
        send_text(client, 405, "Method Not Allowed", "text/plain",
                  "Method not allowed\n");
        return;
    }
    char *query = strchr(path, '?');
    if(query) *query = '\0';
    if(strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        if(access(DASHBOARD_DIR "/index.html", R_OK) == 0) {
            send_file(client, DASHBOARD_DIR "/index.html",
                      "text/html; charset=utf-8");
        } else {
            send_text(client, 200, "OK", "text/html; charset=utf-8",
                      setup_html);
        }
    } else if(strcmp(path, "/summary.json") == 0) {
        send_file(client, TRACKER_SUMMARY_PATH,
                  "application/json; charset=utf-8");
    } else if(strcmp(path, "/backups.json") == 0) {
        if(local_client) tracker_backup_refresh_index();
        send_file(client, TRACKER_BACKUPS_INDEX_PATH,
                  "application/json; charset=utf-8");
    } else if(strcmp(path, "/api/update/status") == 0) {
        char response[OFFLINE_UPDATE_STATUS_SIZE];
        if(offline_update_status_json(response, sizeof(response)) == 0) {
            send_text(client, 200, "OK",
                      "application/json; charset=utf-8", response);
        } else {
            send_text(client, 500, "Internal Server Error",
                      "application/json; charset=utf-8",
                      "{\"ok\":false,\"error\":\"status failed\"}\n");
        }
    } else if(strcmp(path, "/api/setup/status") == 0) {
        char response[OFFLINE_UPDATE_STATUS_SIZE];
        if(offline_setup_status_json(response, sizeof(response)) == 0) {
            send_text(client, 200, "OK",
                      "application/json; charset=utf-8", response);
        } else {
            send_text(client, 500, "Internal Server Error",
                      "application/json; charset=utf-8",
                      "{\"ok\":false,\"error\":\"setup status failed\"}\n");
        }
    } else if(strcmp(path, "/api/access") == 0) {
        send_text(client, 200, "OK",
                  "application/json; charset=utf-8",
                  local_client
                      ? "{\"ok\":true,\"read_only\":false}\n"
                      : "{\"ok\":true,\"read_only\":true}\n");
    } else {
        send_text(client, 404, "Not Found", "text/plain; charset=utf-8",
                  "Not found\n");
    }
}

static void *
server_main(void *unused) {
    struct sockaddr_in address;
    int reuse = 1;
    (void)unused;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0) {
        server_running = 0;
        return NULL;
    }
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(DASHBOARD_HTTP_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) != 0
       || listen(server_fd, 4) != 0) {
        close(server_fd);
        server_fd = -1;
        server_running = 0;
        return NULL;
    }
    while(server_running) {
        struct sockaddr_storage peer;
        socklen_t peer_length = sizeof(peer);
        int client = accept(server_fd, (struct sockaddr *)&peer, &peer_length);
        int local_client = 0;
        if(client < 0) {
            if(server_running && errno == EINTR) continue;
            break;
        }
        if(peer.ss_family == AF_INET) {
            const struct sockaddr_in *ipv4 =
                (const struct sockaddr_in *)&peer;
            local_client = ipv4->sin_addr.s_addr == htonl(0x7f000001u);
        } else if(peer.ss_family == AF_INET6) {
            const struct sockaddr_in6 *ipv6 =
                (const struct sockaddr_in6 *)&peer;
            local_client = IN6_IS_ADDR_LOOPBACK(&ipv6->sin6_addr);
        }
        handle_client(client, local_client, &peer);
        close(client);
    }
    if(server_fd >= 0) close(server_fd);
    server_fd = -1;
    server_running = 0;
    return NULL;
}

int
dashboard_http_start(void) {
    if(server_running) return 0;
    server_running = 1;
    if(pthread_create(&server_thread, NULL, server_main, NULL) != 0) {
        server_running = 0;
        return -1;
    }
    return 0;
}

void
dashboard_http_stop(void) {
    if(!server_running) return;
    server_running = 0;
    if(server_fd >= 0) shutdown(server_fd, SHUT_RDWR);
    pthread_join(server_thread, NULL);
}
