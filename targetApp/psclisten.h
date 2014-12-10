/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef PSCLISTEN_H
#define PSCLISTEN_H

typedef void (*psc_new_client)(int sock, void *pvt);

int psc_tcp_listen(unsigned short port,
                   psc_new_client cb,
                   void *pvt);

#endif // PSCLISTEN_H
