/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef PSCSERVER_H
#define PSCSERVER_H

#define PSCPORT 3000

#define PSCMAXCLIENTS 5

#define PSCHIGHESTBLOCKID 10
#define PSCLONGESTBUFFER 1024

/* LWIP thread stack size */
#define PSCTHRSTACK 0

/* LWIP thread priority */
#define PSCTHRPRIO 0

typedef struct psc_server psc_server;

typedef void (*psc_block_fn)(void *, unsigned short, char *, unsigned long);

psc_server* psc_create_server(void);
void psc_run_server(psc_server *PSC);
void psc_free_server(psc_server *PSC);

int psc_set_recv_block(psc_server *PSC,
                       unsigned short id,
                       unsigned int maxlen,
                       psc_block_fn fn,
                       void *arg);

int psc_send_block(psc_server *PSC,
                   unsigned short id,
                   void *buf,
                   unsigned int len);



#endif // PSCSERVER_H
