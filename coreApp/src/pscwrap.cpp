/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include "psc/device.h"

#include <errlog.h>

#define CATCH(NAME) catch(std::exception& e) {errlogPrintf("%s: " #NAME " error: %s\n", psc->name.c_str(), e.what());}

/* to avoid issues with lock order bufferevent callbacks
 * are run with the bufferevent unlocked (cf. BEV_OPT_UNLOCK_CALLBACKS).
 * Then we re-lock in the correct order.
 */
void PSC::bev_eventcb(bufferevent *, short evt, void *raw)
{
    PSC *psc=(PSC*)raw;
    try{
        Guard g(psc->lock);
        BEVGuard h(psc->session);
        psc->eventcb(evt);
    }CATCH(eventcb)
}

void PSC::bev_datacb(bufferevent *, void *raw)
{
    PSC *psc=(PSC*)raw;
    try{
        Guard g(psc->lock);
        BEVGuard h(psc->session);
        psc->recvdata();
    }CATCH(eventcb)
}

void PSC::bev_reconnect(int, short, void *raw)
{
    PSC *psc=(PSC*)raw;
    try{
        Guard g(psc->lock);
        psc->reconnect();
    }CATCH(eventcb)
}

void PSCUDP::ev_send(int, short evt, void *raw)
{
    PSCUDP *psc=(PSCUDP*)raw;
    try{
        Guard g(psc->lock);
        psc->senddata(evt);
    }CATCH(eventcb)
}

void PSCUDP::ev_recv(int, short evt, void *raw)
{
    PSCUDP *psc=(PSCUDP*)raw;
    try{
        Guard g(psc->lock);
        psc->recvdata(evt);
    }CATCH(eventcb)
}

void PSCBase::ioc_atexit(void *raw)
{
    PSCBase *psc=(PSCBase*)raw;
    try{
        Guard g(psc->lock);
        psc->stop();
    }CATCH(eventcb)
}
