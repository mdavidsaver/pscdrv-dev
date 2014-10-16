/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef EVBASE_H
#define EVBASE_H

#include <tr1/memory>

#include <epicsThread.h>
#include <epicsMutex.h>

#include "util.h"

struct event_base;
struct event;

class EventBase
        :public epicsThreadRunable
        ,public std::tr1::enable_shared_from_this<EventBase>
{
    event_base *base;
    event *keepalive;
    epicsThread runner;
    epicsMutex lock;
    bool running;

    EventBase();

    virtual void run();

    void stop();

    static void keepalive_cb(int,short,void*);
public:
    virtual ~EventBase();

    typedef std::tr1::shared_ptr<EventBase> pointer;
    event_base *get();

    static pointer makeBase();
private:
    static std::tr1::weak_ptr<EventBase> last_base;
};

#endif // EVBASE_H
