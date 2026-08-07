#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "http_server.h"

#define DATA_DIR "/data/ps5-activity"
#define STOP_PATH DATA_DIR "/stop"

#ifndef PROBE_VERSION
#define PROBE_VERSION "lite"
#endif

typedef struct notify_request {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static volatile sig_atomic_t stop_requested;

static int
notify(const char *format, ...) {
    notify_request_t request = {0};
    va_list args;

    va_start(args, format);
    vsnprintf(request.message, sizeof(request.message), format, args);
    va_end(args);
    return sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

int
main(void) {
    struct timespec pause = {1, 0};

    if(mkdir(DATA_DIR, 0755) < 0 && errno != EEXIST) {
        notify("Playlog compatibility test: cannot create data folder");
        return 1;
    }
    unlink(STOP_PATH);
    if(dashboard_http_start() != 0) {
        notify("Playlog compatibility test: HTTP server failed");
        return 1;
    }
    notify("Playlog compatibility test %s started", PROBE_VERSION);
    while(!stop_requested && access(STOP_PATH, F_OK) != 0) {
        nanosleep(&pause, NULL);
    }
    dashboard_http_stop();
    return 0;
}
