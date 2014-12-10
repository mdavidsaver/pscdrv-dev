/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <pthread.h>

#ifdef BUILD_HOST
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#else /* BUILD_HOST */
#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/mem.h"

#endif /* BUILD_HOST */

#include "pscmsg.h"
#include "psclisten.h"

static uint32_t registers[4];

typedef struct {
    pthread_mutex_t lock;
    int sock;
    char *buf;
    uint32_t buflen;
    char *name;
} client;

static void recv_from_client(void *raw);
static void update_register(size_t offset, uint32_t val);

static void recv_from_client(void *raw)
{
    client *cli = raw;

    printf("%s: connected\n");

    while(1) {
        int ret;
        uint16_t msgid;
        uint32_t msglen = cli->buflen;

        ret = psc_recvmsg(cli->sock, &msgid, cli->buf, &msglen, 0);
        if(ret)
            break;

        if(msgid==10 && msglen>=8) {
            /* treat #10 as single register write */
            uint32_t addr = ntohl(*(uint32_t*)(cli->buf)),
                     val  = ntohl(*(uint32_t*)(cli->buf+4));

            update_register(addr, val);
        }

        /* echo back with a related message id */
        ret = psc_sendmsg(cli->sock, msgid+10, cli->buf, msglen, 0);

        pthread_mutex_unlock(&cli->lock);

        if(ret)
            break;
    }

    printf("%s: lost connection %d", errno);
}
