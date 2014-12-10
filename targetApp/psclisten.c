/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <errno.h>
#include <string.h>

#ifdef BUILD_HOST
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#else /* BUILD_HOST */
#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/mem.h"

#endif /* BUILD_HOST */

#include "psclisten.h"

int psc_tcp_listen(unsigned short port,
                   psc_new_client cb,
                   void *pvt)
{
    int sock;
    struct sockaddr_in addr;

    if(port==0)
        return EINVAL;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock==-1)
        goto bail;

    if(bind(sock, (void*)&addr, sizeof(addr)))
        goto closesock;

    if(listen(sock, 4))
        goto closesock;

    while(1) {
        struct sockaddr_in cliaddr;
        socklen_t addrlen = sizeof(cliaddr);
        int client = accept(sock, (void*)&cliaddr, &addrlen);
        if(client==-1) {
            switch(errno) {
            case EINTR:
            case EWOULDBLOCK:
                continue;
            }
            break;
        }

        (*cb)(cliaddr, pvt);
    }

closesock:
    close(sock);
bail:
    return errno;
}
