/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <string.h>

#include <stdexcept>
#include <cstdio>

#include <epicsGuard.h>
#include <epicsThread.h>
#include <epicsMutex.h>

#include <event2/event.h>
#include <event2/buffer.h>

#define epicsExportSharedSymbols
#include "psc/evbase.h"

std::tr1::weak_ptr<EventBase> EventBase::last_base;

EventBase::EventBase()
    :base(NULL)
    ,runner(*this, "eventbase",
            epicsThreadGetStackSize(epicsThreadStackSmall),
            epicsThreadPriorityHigh)
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

dbuffer::dbuffer(size_t n)
    :strides(1u)
    ,backingv(n)
    ,backingb(0u)
{
    strides[0].iov_base = &backingv[0];
    strides[0].iov_len = n;
}

dbuffer::~dbuffer()
{
    clear();
}

size_t dbuffer::size() const
{
    if(backingb)
        return evbuffer_get_length(backingb);
    else
        return backingv.size();
}

void dbuffer::clear()
{
    if(backingb) {
        evbuffer_free(backingb);
        backingb = 0;
    }
    backingv.clear();
    strides.clear();
}

void dbuffer::resize(size_t newlen)
{
    std::vector<evbuffer_iovec> S(1u);
    backingv.reserve(newlen);
    if(backingb) {
        backingv.clear();
        backingv.resize(newlen);

        evbuffer_copyout(backingb, &backingv[0], newlen);

        evbuffer_free(backingb);
        backingb = 0;

    } else {
        backingv.resize(newlen);
    }

    S[0].iov_base = &backingv[0];
    S[0].iov_len = newlen;
    strides.swap(S);
}

void dbuffer::assign(const void *buf, size_t len)
{
    std::vector<evbuffer_iovec> S(1u);
    backingv.resize(len);
    memcpy(&backingv[0], buf, len);

    if(backingb) {
        evbuffer_free(backingb);
        backingb = 0;
    }

    S[0].iov_base = &backingv[0];
    S[0].iov_len = len;
    strides.swap(S);
}

void dbuffer::consume(evbuffer *buf, size_t len)
{
    const size_t total = evbuffer_get_length(buf);
    if(len > total)
        len = total;

    dbuffer temp;

    temp.backingb = evbuffer_new();
    if(!temp.backingb)
        throw std::bad_alloc();

    if(evbuffer_remove_buffer(buf, temp.backingb, len)!=(ev_ssize_t)len)
        throw std::logic_error("consume() move buffer fail");

    temp.strides.resize(2u);

    while(true) {
        size_t nstrides = evbuffer_peek(temp.backingb, len, 0u, &temp.strides[0], temp.strides.size());

        if(nstrides <= temp.strides.size()) {
            temp.strides.resize(nstrides);
            break;
        }

        temp.strides.resize(2u*temp.strides.size());
    }

    swap(temp);
}

template<typename B>
struct dbuffer::stride_ptr {
    B& buf;
    size_t stride, off;

    stride_ptr(B& buf) :buf(buf), stride(0u), off(0u) {}

    size_t copy(size_t n, void *dbase, bool out)
    {
        size_t nmoved = 0u;
        char* dest = (char*)dbase;
        const size_t nstrides = buf.strides.size();

        while(n && stride<nstrides) {
            size_t avail = buf.strides[stride].iov_len - off;

            if(dest) {
                char* src = off + (char*)buf.strides[stride].iov_base;
                size_t ncopy = std::min(n, avail);

                if(out) {
                    memcpy(dest, src, ncopy);
                } else {
                    memcpy(src, dest, ncopy);
                }
                dest += ncopy;
            }

            if(n >= avail) {
                // advance to next stride
                n -= avail;
                nmoved += avail;
                stride++;
                off=0u;

            } else if(valid()) {
                // satisfy from current stride
                off+=n;
                nmoved += n;
                n=0u;

            } else {
                // past end
            }
        }
        return nmoved;
    }

    bool valid() const
    {
        return stride < buf.strides.size();
    }
};

bool dbuffer::copyin(const void *buf, size_t offset, size_t len)
{
    stride_ptr<const dbuffer> ptr(*this);
    ptr.copy(offset, 0u, true); // skip

    return ptr.copy(len, const_cast<void*>(buf), false)==len;
}

size_t dbuffer::copyout_shape(void *rawdest, size_t offset, size_t esize, size_t eskip, size_t ecount) const
{
    const size_t total = size();

    if(ecount==0 || offset >= total)
        return 0u;

    char* dest = (char*)rawdest;

    //size_t needed = offset + esize*ecount + eskip*(ecount-1u);
    //       needed = offset + (esize + eskip)*ecount - eskip;
    size_t actual = (total - offset + eskip)/(esize + eskip);
    if(actual>ecount)
        actual = ecount;

    stride_ptr<const dbuffer> ptr(*this);
    ptr.copy(offset, 0u, true); // skip

    for(size_t e=0u; e<actual; e++) {
        size_t ncopied = ptr.copy(esize, dest, true);
        dest += ncopied;

        if(e < actual-1u)
            ptr.copy(eskip, 0, true); // skip
    }

    return actual;
}

void dbuffer::copyout(evbuffer* dest) const
{
    for(size_t i=0u, N=strides.size(); i<N; i++) {
        if(evbuffer_add(dest, strides[i].iov_base, strides[i].iov_len))
            throw std::runtime_error("copyout() evbuffer_add() error");
    }
}
