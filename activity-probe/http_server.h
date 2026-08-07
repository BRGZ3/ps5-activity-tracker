#ifndef PS5_ACTIVITY_HTTP_SERVER_H
#define PS5_ACTIVITY_HTTP_SERVER_H

#ifndef DASHBOARD_HTTP_PORT
#define DASHBOARD_HTTP_PORT 12888
#endif
#ifndef DASHBOARD_DIR
#define DASHBOARD_DIR "/data/ps5-activity/dashboard"
#endif

int dashboard_http_start(void);
void dashboard_http_stop(void);

#endif
