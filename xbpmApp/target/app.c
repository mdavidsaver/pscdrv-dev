#include <stdio.h>

#include "app.h"

/* Periodically send information to each connected client */
static
void onping(evutil_socket_t sock, short evt, void *raw)
{
    server *serv=raw;
    msgping msg;
    connection *conn;

    serv->cycle_count++;

    msg.counter = htonl(serv->cycle_count);
    msg.fail_cnt = htonl(serv->conn_fail_cnt);

    /* instead of txall() use txconn() to send client specific info */

    for(conn = serv->conn_first; conn; conn = conn->conn_next)
    {
        msg.drop_cnt = htonl(conn->msg_drop_cnt);
        txconn(conn, 42, &msg, sizeof(msg));
    }
}

/* Called on application startup */
int on_startup(server *serv)
{
    serv->ping = event_new(serv->base, -1, EV_TIMEOUT|EV_PERSIST, onping, serv);
    if(!serv->ping) return 1;

    {
        const struct timeval tv = {1,0};
        event_add(serv->ping, &tv); /* start 1Hz timer */
    }

    return 0;
}

/* Called once as each client connects */
int on_connect(connection *conn)
{
    /* TODO: more interesting on-connection message */
    msghello msg;
    msg.version = htonl(42);
    txconn(conn, 43, &msg, sizeof(msg));
    return 0;
}

/* Called once as each client disconnects */
void on_disconnect(connection *conn)
{
}

/* Called for each message received (from any client) */
int on_recv(connection *conn, uint16_t msgid, struct evbuffer *data)
{
    printf("%s: recv'd %u %lu\n", conn->name, msgid,
           (unsigned long)evbuffer_get_length(data));
    return 0;
}
