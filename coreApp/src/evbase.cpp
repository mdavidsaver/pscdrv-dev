/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include "psc/evbase.h"

#include <exception>
#include <cstdio>

#include <epicsGuard.h>

#include <event2/event.h>

std::tr1::weak_ptr<EventBase> EventBase::last_base;

EventBase::EventBase()
    :epicsThreadRunable()
    ,base(NULL)
    ,runner(*this, "eventbase",
            epicsThreadGetStackSize(epicsThreadStackSmall),
            epicsThreadPriorityHigh)
    ,lock()
    ,running(false)
{
    base = event_base_new();
    if(!base)
        throw std::bad_alloc();
    keepalive = evtimer_new(base, keepalive_cb, (void*)this);
    if(!keepalive)
        throw std::bad_alloc();
    running = true;
    runner.start();
}

EventBase::~EventBase()
{
    stop();
    timefprintf(stderr, "%p Loop cleanup\n", (void*)this);
    event_base_free(base);
}

void EventBase::run()
{
    epicsGuard<epicsMutex> g(lock);
    assert(base);
    timeval tv={10000,0};
    evtimer_add(keepalive, &tv);
    {
        epicsGuardRelease<epicsMutex> u(g);
        timefprintf(stderr, "%p Loop start\n", (void*)this);
        event_base_loop(base, 0);
        timefprintf(stderr, "%p Loop stop\n", (void*)this);
    }
    evtimer_del(keepalive);
    running=false;
}

void EventBase::stop()
{
    {
        epicsGuard<epicsMutex> g(lock);
        if(!running)
            return;
    }
    event_base_loopexit(base, NULL);
    runner.exitWait();
}

void EventBase::keepalive_cb(int,short events,void* raw)
{
    EventBase *ebase=(EventBase*)raw;
    timeval tv={10000,0};
    evtimer_add(ebase->keepalive, &tv);
}

event_base* EventBase::get()
{
    epicsGuard<epicsMutex> g(lock);
    return base;
}

/*TODO: not thread safe!*/
EventBase::pointer EventBase::makeBase()
{
    pointer p(last_base.lock());
    if(p)
        return p;
    p.reset(new EventBase);
    last_base=p;
    return p;
}
