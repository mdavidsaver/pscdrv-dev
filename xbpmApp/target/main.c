#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/util.h>

#include "app.h"

/* listening TCP port */
#define PORT 5678

/* maximum RX message body length */
#define MAXMSG 1024

/* maximum TX buffer length before dropping messages */
#define MAXOUT 1024

/* message header */
typedef struct __attribute__((packed)) {
    char P, S;
    uint16_t msgid;
    uint32_t msglen;
} msghead;

static
void freeconn(connection *conn)
{
    /* patch outself out of the connection list */
    if(conn->conn_prev)
        conn->conn_prev->conn_next = conn->conn_next;
    else {
        assert(conn->serv->conn_first==conn);
        conn->serv->conn_first = conn->conn_next;
    }
    if(conn->conn_next)
        conn->conn_next->conn_prev = conn->conn_prev;

    on_disconnect(conn);

    printf("%s: Connection Lost\n", conn->name);

    evbuffer_free(conn->user_buf);
    bufferevent_free(conn->bev);
    free(conn);
}

int txconn(connection *conn, uint16_t msgid, const void* buf, uint32_t len)
{
    msghead head;
    struct evbuffer *txbuf = bufferevent_get_output(conn->bev);

    head.P = 'P';
    head.S = 'S';
    head.msgid = htons(msgid);
    head.msglen = htonl(len);

    if(evbuffer_get_length(txbuf)>MAXOUT ||
       evbuffer_add(txbuf, &head, 8))
    {
        /* TX buffer full, or failed to enqueue header, recoverable */
        conn->msg_drop_cnt++;

    } else if(evbuffer_add(txbuf, buf, len)) {
        /* enqueued header, buf failed to enqueued body,
         * not recoverable
         */
        conn->serv->conn_fail_cnt++;
        freeconn(conn);
        return 1;
    }
    return 0;
}

void txall(server *serv, uint16_t msgid, const void* buf, uint32_t len)
{
    connection *conn;
    for(conn = serv->conn_first; conn; conn = conn->conn_next)
    {
        txconn(conn, msgid, buf, len);
    }
}

static
void connread(struct bufferevent *bev, void *raw)
{
    connection *conn=raw;
    struct evbuffer *rxbuf = bufferevent_get_input(bev);

    assert(bev==conn->bev);

    while(1) {
        /* how many bytes needed to continue */
        size_t expect = conn->havehead ? 8 : conn->msglen;
        size_t mlen = evbuffer_get_length(rxbuf);

        if(mlen<expect) {
            /* need to wait for more bytes */

            assert(expect<MAXMSG+1);
            bufferevent_setwatermark(bev, EV_READ, expect, MAXMSG+1);
            break;
        }

        if(!conn->havehead) {
            /* process message header */

            msghead head;
            assert(mlen>=8);
            evbuffer_remove(rxbuf, &head, 8);

            if(head.P!='P' || head.S!='S') {
                printf("%s: malformed header! %x %x\n", conn->name, head.P, head.S);
                freeconn(conn);
                return;
            }
            head.msgid = ntohs(head.msgid);
            head.msglen = ntohl(head.msglen);

            if(head.msglen>MAXMSG) {
                printf("%s: length exceeds MAXMSG %lu > %lu\n",
                       conn->name, (unsigned long)head.msglen, (unsigned long)MAXMSG);
                freeconn(conn);
                return;
            }

            conn->havehead = 1;
            conn->msgid = head.msgid;
            conn->msglen = head.msglen;

        } else {
            size_t L;
            /* process message body */
            assert(mlen>=conn->msglen);
            conn->havehead = 0;

            if((L=evbuffer_get_length(conn->user_buf))>0)
                evbuffer_drain(conn->user_buf, L);

            evbuffer_remove_buffer(rxbuf, conn->user_buf, conn->msglen);

            /* TODO: something more interesting */
            evbuffer_drain(rxbuf, conn->msglen);
        }

    }
}

static
void connevent(struct bufferevent *bev, short what, void *raw)
{
    connection *conn=raw;

    if(what&(BEV_EVENT_ERROR|BEV_EVENT_EOF|BEV_EVENT_TIMEOUT)) {
        /* connection ends */
        printf("%s: event ", conn->name);

        if(what&BEV_EVENT_ERROR)
            printf("error: %s ",
                   evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));

        if(what&BEV_EVENT_TIMEOUT) {
            if(what&BEV_EVENT_READING)
                printf("timeout reading ");
            if(what&BEV_EVENT_WRITING)
                printf("timeout writing ");
        }

        if(what&BEV_EVENT_EOF)
            printf("disconnect ");

        printf("\n");

    } else
        printf("%s: unknown event 0x%x ", conn->name, what);

    freeconn(conn);
}

/* handle new connection to listening port */
static
void onconn(struct evconnlistener *lev, evutil_socket_t sock,
            struct sockaddr *src, int socklen, void *raw)
{
    server *serv=raw;
    connection *conn;
    struct sockaddr_in addr4;
    uint32_t ip;

    conn = calloc(1,sizeof(*conn));
    if(!conn || src->sa_family!=AF_INET || socklen>sizeof(addr4))
        goto fail;
    conn->serv = serv;

    /* format the client IP:PORT for future debug messages */
    memcpy(&addr4, src, socklen);
    ip = addr4.sin_addr.s_addr;
    sprintf(conn->name, "%u.%u.%u.%u:%u",
            (ip>>24)&0xff, (ip>>16)&0xff,(ip>>8)&0xff, ip&0xff,
            addr4.sin_port);

    conn->bev = bufferevent_socket_new(serv->base, sock, BEV_OPT_CLOSE_ON_FREE);
    conn->user_buf = evbuffer_new();
    if(!conn->bev || !conn->user_buf)
        goto fail;


    bufferevent_setcb(conn->bev, connread, NULL, connevent, conn);
    {
        const struct timeval tv = {5,0};
        bufferevent_set_timeouts(conn->bev, &tv, &tv);
    }
    /* prepare to recv first header */
    bufferevent_setwatermark(conn->bev, EV_READ, 8, MAXMSG+1);

    if(on_connect(conn)) {
        printf("%s: connect fails with user error\n", conn->name);
        goto fail;
    }

    bufferevent_enable(conn->bev, EV_READ);

    /* prepend ourself to the connection list */
    if(serv->conn_first)
        serv->conn_first->conn_prev = conn;
    conn->conn_next = serv->conn_first;
    serv->conn_first = conn;

    printf("%s: Connection established\n", conn->name);
    return;

fail:
    if(conn->bev)
        bufferevent_free(conn->bev);
    else
        evutil_closesocket(sock);
    if(conn->user_buf)
        evbuffer_free(conn->user_buf);
    free(conn);
    serv->conn_fail_cnt++;
    return;
}

/* error on listening port */
static
void onconnerr(struct evconnlistener *lev, void *raw)
{
    printf("Error in listening socket: %s\n",
           evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
}

int main(void)
{
    struct sockaddr_in addr;
    server serv;

    assert(sizeof(msghead)==8);

    memset(&serv, 0, sizeof(serv));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    serv.base = event_base_new();
    if(!serv.base) return 42;

    serv.listener = evconnlistener_new_bind(serv.base, onconn, &serv,
                                            LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, 4,
                                            (void*)&addr,sizeof(addr));
    if(!serv.listener) return 44;
    evconnlistener_set_error_cb(serv.listener, onconnerr);

    if(on_startup(&serv)) return 45;

    printf("Running\n");
    event_base_dispatch(serv.base);

    return 0;
}

