/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef EVBASE_H
#define EVBASE_H

#if __cplusplus>=201103L || (defined(_MSC_VER) && (_MSC_VER>=1600)) || defined(_LIBCPP_VERSION)
#  include <memory>
using ::std::shared_ptr;
using ::std::weak_ptr;
using ::std::enable_shared_from_this;
#else
#  include <tr1/memory>
using ::std::tr1::shared_ptr;
using ::std::tr1::weak_ptr;
using ::std::tr1::enable_shared_from_this;
#endif

#include <vector>

#include <epicsThread.h>
#include <epicsMutex.h>

#include <event2/buffer.h>

#include "util.h"

#include <shareLib.h>

struct event_base;
struct event;

class epicsShareClass EventBase
        :private epicsThreadRunable
        ,public enable_shared_from_this<EventBase>
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

    typedef shared_ptr<EventBase> pointer;
    event_base *get();

    static pointer makeBase();
private:
    static weak_ptr<EventBase> last_base;
};


//! Dis-contiguous byte buffer.  Backed by either an evbuffer or vector<char>
class epicsShareClass dbuffer
{
    std::vector<evbuffer_iovec> strides;
    std::vector<char> backingv;
    evbuffer* backingb;
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

    /** Copy bytes in
     * @param buf Input pointer
     * @param offset In bytes into this buffer
     * @param len In bytes to copy
     * @return true on success
     */
    bool copyin(const void *buf, size_t offset, size_t len);

    bool copyout(void *dest, size_t offset, size_t nbytes) const {
        return copyout_shape(dest, offset, nbytes, 0u, 0u, 1u)==1u;
    }
    /** Copy out array.
     *
     * @param dest Output pointer
     * @param ioffset In bytes into this buffer
     * @param esize size of dest[] elements
     * @param iskip Input bytes to skip after each element
     * @param dskip Output bytes to skip after each element
     * @param ecount Number of elements to copy
     * @returns Number of complete elements copied.
     */
    size_t copyout_shape(void *dest, size_t ioffset, size_t esize, size_t iskip, size_t dskip, size_t ecount) const;

    void copyout(evbuffer* dest) const;
};

#endif // EVBASE_H
