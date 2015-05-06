#ifndef APP_H
#define APP_H

#include <stdlib.h>
#include <inttypes.h>

#include <event2/event.h>
#include <event2/buffer.h>

typedef struct server server;
typedef struct connection connection;

struct server {
    struct event_base *base;
    struct evconnlistener *listener;

    connection *conn_first; /* head of connection list */

    uint32_t conn_fail_cnt; /* connection error counter */
    uint32_t cycle_count;

    /* add app specific server-wide after here */

    struct event *ping;
};

struct connection {
    server *serv;
    connection *conn_next, *conn_prev;

    struct bufferevent *bev; /* socket */
    struct evbuffer *user_buf;

    unsigned int havehead:1; /* header RX decode state */

    // when havehead==1
    uint16_t msgid;
    uint32_t msglen;

    uint32_t msg_drop_cnt; /* # of messages dropped due to TX buffer full */

    char name[40]; /* client name */

    /* add app specific per-client after here */
};

/* actions */

/* Send a message to a client
 @returns non-zero on failure, also the conn is free'd
 */
int txconn(connection *conn, uint16_t msgid, const void* buf, uint32_t len);
/* Send a message to all connected clients
 */
void txall(server *serv, uint16_t msgid, const void* buf, uint32_t len);

/* app callbacks */

int on_startup(server *serv);
int on_connect(connection *conn);
void on_disconnect(connection *conn);

int on_recv(connection *conn, uint16_t msgid, struct evbuffer *data);

/* app message definitions */

/* periodic ping message */
typedef struct __attribute__((packed)) {
    uint32_t counter;
    uint32_t fail_cnt;
    uint32_t drop_cnt;
} msgping;

/* initial message */
typedef struct __attribute__((packed)) {
    uint32_t version;
} msghello;

#endif // APP_H
