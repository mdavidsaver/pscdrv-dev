/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <string.h>

#ifdef BUILD_HOST
#include <sys/types.h>
#include <sys/socket.h>

#include "sys_host_compat.h"

#define lwip_htons
#define lwip_htonl
#define lwip_ntohs
#define lwip_ntohl

#define lwip_socket socket
#define lwip_bind bind
#define lwip_listen listen
#define lwip_accept accept
#define lwip_recv recv
#define lwip_send send
#define lwip_getsockopt getsockopt
#define lwip_setsockopt setsockopt

#define mem_free free
#define mem_malloc malloc
#define mem_calloc calloc

#else /* BUILD_HOST */
#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/mem.h"

#endif /* BUILD_HOST */

#include "pscserver.h"

typedef struct psc_block  psc_block;
typedef struct psc_client psc_client;

struct psc_block {
    /*psc_client *client;*/
    unsigned short id;
    unsigned int maxlen, len;
    char *buf;

    psc_block_fn fn;
    void *arg;
};

struct psc_client {
    psc_client *next, *prev;
    sys_mutex_t lock;

    unsigned int refs;

    psc_server *server;
    int socket;
};

struct psc_server {
    int socket;
    sys_mutex_t lock;

    unsigned int stop;

    psc_client *first, *last;
    unsigned int numclient;

    psc_block blocks[PSCHIGHESTBLOCKID-1];
};

static void psc_main(void *);

psc_server* psc_create_server(void)
{
    struct sockaddr_in laddr;
    psc_server* PSC = mem_calloc(sizeof(*PSC));

    memset(&laddr, 0, sizeof(laddr));
    laddr.sin_len = sizeof(laddr);
    laddr.sin_family = AF_INET;
    laddr.sin_port = lwip_htons(PSCPORT);
    laddr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);

    if(!PSC)
        return NULL;

    if(sys_mutex_init(PSC->lock))
        goto dofree;

    PSC.socket = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if(PSC.socket==-1)
        goto unmutex;

    if(lwip_bind(PSC.socket, (void*)&laddr, sizeof(laddr))==-1)
        goto doclose;

    if(lwip_listen(PSC.socket, 2)==-1)
        goto doclose;

    return PSC;

doclose:
    close(PSC.socket);
unmutex:
    sys_mutex_free(PSC->lock);
dofree:
    mem_free(PSC);
    return NULL;
}

void psc_run_server(psc_server *PSC)
{
    while(1) {
        psc_client *newclient;
        struct sockaddr_in peeraddr;
        socklen_t slen = sizeof(laddr);
        int client;

        if((client = lwip_accept(PSC.socket, (void*)&peeraddr, &slen))==-1)
            break;

        sys_mutex_lock(PSC->lock);
        if(PSC.numclient>=PSCMAXCLIENTS) {
            sys_mutex_unlock(PSC->lock);
            goto dropclient;
        }
        sys_mutex_unlock(PSC->lock);

        newclient = mem_malloc(*newclient);
        if(!newclient)
            goto dropclient;

        newclient->server = &PSC;
        newclient->refs = 1;

        if(sys_mutex_init(&newclient->lock))
            goto freeclient;

        if(sys_thread_new("pscrecv", &psc_main, newclient, PSCTHRSTACK, PSCTHRPRIO)==NULL)
            goto freemutex;

        sys_mutex_lock(PSC->lock);
        /* append new client */
        newclient->next = NULL;
        newclient->prev = PSC->last;
        PSC->last = newclient;
        PSC->numclient++;
        sys_mutex_unlock(PSC->lock);

        continue;
    freemutex:
        sys_mutex_free(&newclient->lock);
    freeclient:
        mem_free(newclient);
    dropclient:
        close(client);
    }
}

void psc_free_server(psc_server *PSC)
{
    sys_mutex_lock(PSC->lock);
    PSC->stop = 1;
    while(PSC->numclient) {
        sys_mutex_unlock(PSC->lock);
        sys_msleep(100);
        sys_mutex_lock(PSC->lock);
    }
    sys_mutex_unlock(PSC->lock);

    close(PSC.socket);
    sys_mutex_free(PSC->lock);

}

static int sendN(int socket, void *buf, size_t len)
{
    size_t txd = 0;
    while(txd<len) {
        int ret = lwip_send(socket, buf+txd, len-txd, 0);
        if(ret<=0)
            return ret;
        txd += ret;
    }
    return 1;
}

static int recvN(int socket, void *buf, size_t len)
{
    size_t rxd = 0;

    while(rxd<len) {
        int ret = lwip_recv(socket, buf+rxd, len-rxd, 0);
        if(ret<=0)
            return ret;
        rxd += ret;
    }
    return 1;
}

static int drainN(int socket, size_t len)
{
    char buf[128];

    while(len>=sizeof(buf)) {
        int ret = recvN(socket, buf, sizeof(buf));
        if(ret!=1)
            return ret;
        len-=sizeof(buf);
    }

    if(len)
        return recvN(socket, buf, len);
}

struct psc_header {
    char P;
    char S;
    u16_t mid;
    u32_t mlen;
};

union psc_header_buf {
    psc_header M;
    char B[sizeof(psc_header)];
};

static void psc_client_incref(psc_client *self)
{
    psc_server *PSC = self->server;

    sys_mutex_lock(PSC->lock);
    assert(self->refs>0);
    self->refs++;
    sys_mutex_unlock(PSC->lock);
}

static void psc_client_decref(psc_client *self)
{
    unsigned int newrefs;
    psc_server *PSC = self->server;

    sys_mutex_lock(PSC->lock);

    assert(self->refs>0);
    newrefs = --self->refs;
    if(newrefs) {
        sys_mutex_unlock(PSC->lock);
        return;
    }

    /* remove ourself from the list of clients */
    if(PSC->first==self)
        PSC->first=self->next;
    if(PSC->last==self)
        PSC->last==self->prev;
    if(self->prev)
        self->prev->next = self->next;
    if(self->next)
        self->next->prev = self->prev;
    self->server->numclient--;

    sys_mutex_unlock(self->server->lock);

    sys_mutex_free(&self->lock);
    close(self->client);
    mem_free(self);

}

static void psc_main(void *raw)
{
    psc_client *self = raw;
    psc_server *PSC = self->server;

    while(1) {
        u32_t skip = 0;
        psc_header_buf buf;

        if(recvN(self->socket, buf.B, sizeof(buf.B))!=1)
            break;

        if(buf.M.P!='P' || buf.M.S!='S')
            break; /* framing error */

        buf.M.mid = lwip_ntohs(buf.M.mid);
        buf.M.mlen= lwip_ntohl(buf.M.mlen);

        if(buf.M.mid>PSCHIGHESTBLOCKID || buf.M.mlen>PSCLONGESTBUFFER)
            skip = buf.M.mlen;
        else {
            psc_block *blk = &PSC->blocks[buf.M.mid];
            if(blk->maxlen<buf.M.mlen) {
                skip = buf.M.mlen - blk->maxlen;
                blk->len = blk->maxlen;
            } else
                blk->len = buf.M.mlen;

            if(recvN(self->socket, blk->buf, nrx)!=1)
                break;

            if(blk->fn)
                (*blk->fn)(blk->arg, buf.M.mid, blk->buf, blk->len);
        }
    }

    psc_client_decref(self);
}

/* NOT safe to call in parallel to psc_run_server() */
int psc_set_recv_block(psc_server *PSC,
                       unsigned short id,
                       unsigned int maxlen,
                       psc_block_fn fn,
                       void *arg)
{
    psc_block *blk;
    if(id>PSCHIGHESTBLOCKID)
        return 1;

    blk = &PSC->blocks[id];

    if(maxlen!=blk->maxlen) {
        blk->buf = mem_calloc(1, maxlen);
        if(!blk->buf) {
            sys_mutex_unlock(PSC->lock);
            return 1;
        }
        blk->fn = fn;
        blk->arg = arg;
    }

    return 0;
}

int psc_send_block(psc_server *PSC,
                   unsigned short id,
                   void *buf,
                   unsigned int len)
{
    psc_header_buf buf;
    psc_client *cur = NULL;

    if(id>PSCHIGHESTBLOCKID || len>PSCLONGESTBUFFER)
        return 1;

    buf.M.P = 'P';
    buf.M.S = 'S';
    buf.M.mid = lwip_htons(id);
    buf.M.mlen = lwip_htonl(len);

    while(1) {
        if(cur) {
            psc_client *next;
            sys_mutex_lock(PSC->lock);
            next = cur->next;
            assert(next->refs>0);
            next->refs++;
            sys_mutex_unlock(PSC->lock);
            psc_client_decref(cur);
            cur = next;
        } else {
            sys_mutex_lock(PSC->lock);
            cur = PSC->first;
            sys_mutex_unlock(PSC->lock);
        }

        if(!cur)
            break; /* last client found */

        sys_mutex_lock(cur->lock);

        if(sendN(cur->socket, buf.B, sizeof(buf.B))!=1 ||
                sendN(cur->socket, buf, len)!=1)
        {
            close(cur->socket);
            cur->socket = -1;
        }


        sys_mutex_unlock(cur->lock);
    }

}
