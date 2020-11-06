/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef EVBASE_H
#define EVBASE_H

#include <vector>
#include <tr1/memory>

#include <epicsThread.h>
#include <epicsMutex.h>

#include <event2/buffer.h>

#include "util.h"

#include <shareLib.h>

struct event_base;
struct event;

class epicsShareClass EventBase
        :private epicsThreadRunable
        ,public std::tr1::enable_shared_from_this<EventBase>
{
    event_base *base;
    event *keepalive;
    epicsThread runner;
    epicsMutex lock;
    bool running;

    EventBase();

private:
    virtual void run();
public:

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


//! Dis-contiguous byte buffer
class epicsShareClass dbuffer
{
    std::vector<evbuffer_iovec> strides;
    std::vector<char> backingv;
    evbuffer* backingb;
    template<typename B>
    struct stride_ptr;

    dbuffer(const dbuffer&);
    dbuffer& operator=(const dbuffer&);
public:
    dbuffer() :backingb(0) {}
    dbuffer(size_t n);
    ~dbuffer();

    void swap(dbuffer& o)
    {
        if(this!=&o) {
            strides.swap(o.strides);
            backingv.swap(o.backingv);
            std::swap(backingb, o.backingb);
        }
    }

    size_t size() const;
    size_t nstrides() const { return strides.size(); }

    void clear();
    void resize(size_t newlen);

    // resize and copy in
    void assign(const void *buf, size_t len);

    // move contents in.  Removes 'len' bytes from input evbuffer
    void consume(evbuffer *buf, size_t len=(size_t)-1);

    bool copyin(const void *buf, size_t offset, size_t len);

    bool copyout(void *dest, size_t offset, size_t nbytes) const {
        return copyout_shape(dest, offset, nbytes, 0u, 1u)==1u;
    }
    // returns number of elements copied
    size_t copyout_shape(void *dest, size_t offset, size_t esize, size_t eskip, size_t ecount) const;

    void copyout(evbuffer* dest) const;
};

#endif // EVBASE_H
