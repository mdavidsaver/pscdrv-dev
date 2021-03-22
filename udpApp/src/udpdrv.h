/*************************************************************************\
* Copyright (c) 2021 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef UDPDRV_H
#define UDPDRV_H

#include <osiSock.h>
#include <osiUnistd.h>

#include <psc/device.h>

struct UDPFast : public PSCBase
{
    SOCKET sock;
    osiSockAddr self, peer;

    int running;
    size_t batchSize;
    size_t vpoolTotal;
    size_t rxcnt;
    size_t ntimeout;
    size_t ndrops;
    size_t noom;
    size_t lastsize;

    size_t netrx;
    size_t storewrote;

    typedef std::vector<std::vector<char> > vecs_t;
    // vector data free-list
    // entries originating with this free-list may appear in:
    //   vpool
    //   pending
    //   inprog - local to rxfn()
    // guarded by rxLock
    vecs_t vpool;

    struct pkt {
        std::vector<char> body;
        size_t bodylen;
        epicsTimeStamp rxtime;
        epicsUInt16 msgid;
    };

    // guarded by rxLock
    typedef std::vector<pkt> pkts_t;
    pkts_t pending;

    epicsEvent vpoolStall;
    epicsEvent pendingReady; // set from rxWorker to wake cacheWorker

    std::string filedir, filebase;
    std::string lastfile;
    std::string lasterror;
    bool reopen;
    bool record;

    // rx worker pulls from socket buffer and pushes to 'pending'
    struct RXWorker : public epicsThreadRunable
    {
        UDPFast * const self;
        explicit RXWorker(UDPFast* self) : self(self) {}
        virtual ~RXWorker() {}
        virtual void run() override final { self->rxfn(); }
    } rxjob;
    epicsThread rxworker;
    mutable epicsMutex rxLock;

    // cache worker pulls from 'pending' and pushes to Block cache
    struct CacheWorker : public epicsThreadRunable
    {
        UDPFast *self;
        explicit CacheWorker(UDPFast* self) : self(self) {}
        virtual ~CacheWorker() {}
        virtual void run() override final { self->cachefn(); }
    } cachejob;
    epicsThread cacheworker;

    UDPFast(const std::string& name,
            const std::string& host,
            unsigned short port,
            unsigned short bindport);

    virtual ~UDPFast();

    void rxfn();;

    void cachefn();

    virtual void connect() override final;
    virtual void stop() override final;

    virtual void queueSend(epicsUInt16, const void *, epicsUInt32) override final {}
    virtual void queueSend(Block *, const dbuffer &) override final {}
    virtual void queueSend(Block *, const void *, epicsUInt32) override final {}
    virtual void flushSend() override final {}
    virtual void forceReConnect() override final {}

    virtual void report(int lvl) override final {}
};

#endif // UDPDRV_H
