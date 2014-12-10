
#include <stdio.h>
#include <inttypes.h>

#include <lwip/init.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <lwip/mem.h>

#include <lwip/sys.h>

#include "pscopts.h"
#include "pscserver.h"
#include "pscmsg.h"

struct psc_client {
    struct psc_client *prev;
    struct psc_client *next;
    int active;

    int sock;
    struct sockaddr_in peeraddr;

    psc_key *PSC;

    char *rxbuf;
};

struct psc_key {
    sys_mutex_t sendguard;
    const psc_config *conf;

    int listen_sock;

    unsigned client_count;
    psc_client *client_head;
};

static void handle_client(void *raw);

#define ERROR(BAD, fmt, ...) \
    do{if(BAD) { \
    printf("Error: %s:%d %s" #BAD ": " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); return; \
    }}while(0)

#define PERROR(BAD, fmt, ...) \
    do{if(BAD) { \
    printf("Error: %s:%d %s (errno=%d): " fmt "\n", __FILE__, __LINE__, __FUNCTION__, errno, ##__VA_ARGS__); return; \
    }}while(0)

void psc_run(psc_key **key, const psc_config *config)
{
    struct sockaddr_in laddr;
    psc_key *PSC;

    memset(&laddr, 0, sizeof(laddr));

    laddr.sin_family = AF_INET;
    laddr.sin_addr.s_addr = htonl(INADDR_ANY);
    laddr.sin_port = htons(config->port);

    ERROR(key && *key, "key already set");

    PSC = calloc(1, sizeof(*PSC));
    ERROR(!PSC, "Allocation");
    PSC->conf = config;

    PERROR(sys_mutex_new(&PSC->sendguard)!=ERR_OK, "sendguard");

    PSC->listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    PERROR(PSC->listen_sock==-1, "socket()");

    PERROR(bind(PSC->listen_sock, (void*)&laddr, sizeof(laddr))==-1,
          "bind to port %d", config->port);

    PERROR(listen(PSC->listen_sock, 2)==-1, "listen");

    if(key)
        *key = PSC;

    printf("Server ready on port %d\n", config->port);
    while(1) {
        psc_client *C = NULL;
        char *Cbuf = NULL;
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);

        int client = accept(PSC->listen_sock, (void*)&caddr, &clen);

#if LWIP_SO_SNDTIMEO
        /* SO_SNDTIMEO isn't supported in LWIP 1.4.0 */
        {
            int val = 1000; /* ms */
            if(setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &val, sizeof(val))==-1)
                printf("Can't set TX timeout\n");
        }
#else
#  warning TX timeout not enabled
#endif
#if LWIP_SO_RCVTIMEO
        {
            int val = 5000; /* ms */
            if(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &val, sizeof(val))==-1)
                printf("Can't set RX timeout\n");
        }
#else
#  warning RX timeout not enabled
#endif

        if(client==-1) {
            printf("accept error %d for port %d\n", errno, config->port);
            sys_msleep(1000);

        } else if(PSC->client_count>=PSC_MAX_CLIENTS ||
                  !(C = calloc(1,sizeof(*C))) ||
                  !(Cbuf = malloc(PSC_MAX_RX_MSG_LEN)))
        {
            printf("Dropping client %s:%d (%d connected)\n",
                   inet_ntoa(caddr.sin_addr.s_addr),
                   ntohs(caddr.sin_port),
                   PSC->client_count);
            close(client);
            free(C);
            free(Cbuf);
        } else {
            C->PSC = PSC;
            C->rxbuf = Cbuf;
            C->sock = client;
            C->peeraddr = caddr;

            if(!sys_thread_new("psc", handle_client, C, DEFAULT_THREAD_STACKSIZE, config->client_prio)) {
                printf("Failed to start client thread!\n");
                close(client);
                free(C);
                free(Cbuf);

            } else {
                sys_mutex_lock(&PSC->sendguard);
                C->next = PSC->client_head;
                PSC->client_head = C;
                PSC->client_count++;
                C->active = 1;
                sys_mutex_unlock(&PSC->sendguard);

                printf("New client %s:%d (%d connected)\n",
                       inet_ntoa(caddr.sin_addr.s_addr),
                       ntohs(caddr.sin_port),
                       PSC->client_count);
            }
        }
    }

    if(key)
        *key = NULL;
}

static void handle_client(void *raw)
{
    psc_client *C = raw;

    if(C->PSC->conf->conn)
        (*C->PSC->conf->conn)(C->PSC->conf->pvt, PSC_CONN, C);

    while(1) {
        uint16_t msgid;
        uint32_t msglen = PSC_MAX_RX_MSG_LEN;
        if(psc_recvmsg(C->sock, &msgid, C->rxbuf, &msglen, 0))
            break; /* read error */

        (*C->PSC->conf->recv)(C->PSC->conf->pvt, msgid, msglen, C->rxbuf);
    }

    /* patch outselves out of the client list */
    sys_mutex_lock(&C->PSC->sendguard);
    if(C->next)
        C->next->prev = C->prev;
    if(C->prev)
        C->prev->next = C->next;
    else
        C->PSC->client_head = C->next;
    C->PSC->client_count--;
    sys_mutex_unlock(&C->PSC->sendguard);

    if(C->PSC->conf->conn)
        (*C->PSC->conf->conn)(C->PSC->conf->pvt, PSC_DIS, C);

    printf("client disconnect %s:%d (%d connected)\n",
           inet_ntoa(C->peeraddr.sin_addr.s_addr),
           ntohs(C->peeraddr.sin_port),
           C->PSC->client_count);

    sys_mutex_lock(&C->PSC->sendguard);
    close(C->sock);
    sys_mutex_unlock(&C->PSC->sendguard);

    free(C->rxbuf);
    free(C);
}


void psc_send(psc_key *PSC, uint16_t msgid, uint32_t msglen, void *msg)
{
    psc_client *C;
    if(!PSC)
        return;
    sys_mutex_lock(&PSC->sendguard);
    for(C=PSC->client_head; C; C=C->next) {
        int ret;
        if(!C->active)
            continue;
        ret = psc_sendmsg(C->sock, msgid, msg, msglen, 0);
        if(ret) {
            printf("%s senderror errno=%d\n", __FUNCTION__, errno);
            /* client error */
            C->active = 0;
            close(C->sock); /* will wake up RX thread */
        }
    }
    sys_mutex_unlock(&PSC->sendguard);
}

void psc_send_one(psc_client *C, uint16_t msgid, uint32_t msglen, void *msg)
{
    sys_mutex_lock(&C->PSC->sendguard);
    int ret = psc_sendmsg(C->sock, msgid, msg, msglen, 0);
    if(ret) {
        printf("%s senderror errno=%d\n", __FUNCTION__, errno);
        /* client error */
        C->active = 0;
        close(C->sock); /* will wake up RX thread */
    }
    sys_mutex_unlock(&C->PSC->sendguard);
}
