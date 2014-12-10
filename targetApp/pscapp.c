
#include <stdio.h>

#include <lwip/init.h>
#include <arch/sys_arch.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>

#include <netif/etharp.h>
#include <netif/tapif.h>

#include "pscserver.h"

static struct netif netif;

static void net_setup(void *raw)
{
    sys_sem_t *startup = raw;

    ip_addr_t ipaddr, netmask, gw;

    IP4_ADDR(&gw, 192,168,0,1);
    IP4_ADDR(&ipaddr, 192,168,0,2);
    IP4_ADDR(&netmask, 255,255,255,0);

    printf("net_setup\n");

    //lwip_init();

    netif_add(&netif, &ipaddr, &netmask, &gw, NULL, tapif_init, tcpip_input);
    netif_set_default(&netif);
    netif_set_up(&netif);

    printf("net_setup done\n");
    sys_sem_signal(startup);
}

static psc_key *key;

static uint32_t clicount;
static uint32_t sendperiodic = 1;

static void onconn(void *pvt, psc_event evt, psc_client *cli)
{
    if(evt!=PSC_CONN)
        return;
    psc_send_one(cli, 42, 12, "hello world!");
    psc_send_one(cli, 55, sizeof(clicount), &clicount);
}

static void rxmsg(void *pvt, uint16_t msgid, uint32_t msglen, void *msg)
{
    if(msgid!=99 || msglen!=4)
        psc_send(key, msgid+10, msglen, msg);
    else {
        sendperiodic = *(uint32_t*)msg;
        printf("%sable periodic\n", sendperiodic?"en":"dis");
    }
}

static psc_config conf = {
    .port=90,
    .recv=rxmsg,
    .conn=onconn,
};

static void periodic(void *raw)
{
    uint32_t count=0;

    while(1) {
        sys_msleep(2000);
        if(sendperiodic)
            psc_send(key, 14, sizeof(count), &count);
        count++;
    }
}

int main()
{
    sys_sem_t startup;
    if(sys_sem_new(&startup, 0)!=ERR_OK)
        return 42;

    tcpip_init(net_setup, &startup);
    sys_sem_wait(&startup);
    sys_sem_free(&startup);

    if(!sys_thread_new("periodic", periodic, NULL, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO)) {
        printf("Failed to start period thread\n");
        return 1;
    }
    printf("Initialized\n");

    psc_run(&key, &conf);

    return 0;
}
