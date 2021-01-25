/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef PSC_H
#define PSC_H

#include "evbase.h"

#include <epicsTypes.h>
#include <dbScan.h>
#include <epicsGuard.h>
#include <epicsEvent.h>
#include <epicsTime.h>
#include <dbCommon.h>

#include <string>
#include <map>
#include <vector>
#include <set>
#include <exception>

#include <event2/event.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include "cblist.h"

#if __cplusplus<201103L
#  ifndef override
#    define override
#  endif
#  ifndef final
#    define final
#  endif
#endif

extern "C" {
extern int PSCDebug;
extern int PSCInactivityTime;
extern int PSCMaxSendBuffer;
}

class recAlarm : public std::exception
{
public:
    short status, severity;
    recAlarm();
    recAlarm(short sts, short sevr);
    virtual const char *what();
};

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

class BEVGuard
{
    bufferevent *bev;
public:
    BEVGuard(bufferevent *b) : bev(b)
    {
        bufferevent_lock(bev);
    }
    ~BEVGuard()
    {
        bufferevent_unlock(bev);
    }
};

class dbCommon;
class PSCBase;

struct Block
{
    PSCBase& psc;
    const epicsUInt16 code;

    dbuffer data;

    bool queued;

    IOSCANPVT scan;
    // bit mask of callback.h priority for in-progress scan
    unsigned scanBusy;
    // requestScan() called again while previous scan in progress
    bool scanQueued;

    CBList<Block> listeners;

    epicsUInt32 count; // TX or RX counter
    epicsUInt32 scanCount;
    epicsUInt32 scanOflow;

    epicsTime rxtime; // RX timestamp

    Block(PSCBase*, epicsUInt16);

    void requestScan();
private:
    static
    void scanned(void *usr, IOSCANPVT, int prio);
};

// User code must lock PSCBase::lock before
// any access to methods or other members.
class PSCBase
{
public:
    const std::string name;
    const std::string host;
    const unsigned short port;

    typedef std::map<epicsUInt16, Block*> block_map;

protected:
    bool connected;

    epicsUInt32 ukncount; // RX counter for unknown blocks
    epicsUInt32 conncount; // # of successful connections

    block_map send_blocks, recv_blocks;

public:
    PSCBase(const std::string& name,
        const std::string& host,
        unsigned short port);
    virtual ~PSCBase();

    mutable epicsMutex lock;

    Block* getSend(epicsUInt16);
    Block* getRecv(epicsUInt16);

    void send(epicsUInt16);
    virtual void queueSend(epicsUInt16, const void*, epicsUInt32) =0;
    virtual void queueSend(Block*, const dbuffer&) =0;
    virtual void queueSend(Block*, const void*, epicsUInt32) =0;

    virtual void connect()=0;
    virtual void flushSend()=0;
    virtual void forceReConnect()=0;

    inline bool isConnected() const{return connected;}
    inline std::string lastMessage() const{return message;}

    inline epicsUInt32 getUnknownCount() const {return ukncount;}
    inline epicsUInt32 getConnCount() const {return conncount;}

    std::string message;
    IOSCANPVT scan;
    IOSCANPVT onConnect;
    std::vector<dbCommon*> procOnConnect;

    virtual void report(int lvl);
protected:
    typedef std::map<std::string, PSCBase*> pscmap_t;
    static pscmap_t pscmap;

public:
    static void startAll();
    static PSCBase* getPSCBase(const std::string&);
    template<typename T>
    static T* getPSC(const std::string& n)
    {
        PSCBase* b = getPSCBase(n);
        if(!b) return 0;
        else return dynamic_cast<T*>(b);
    }

    template<typename FN, typename ARG>
    static bool visit(FN fn, ARG arg) {
        bool val = true;
        pscmap_t::const_iterator it, end = pscmap.end();
        for(it=pscmap.begin(); it!=end; ++it) {
            val = fn(arg, it->second);
            if(!val) break;
        }
        return val;
    }

    static
    bool ReportOne(int lvl, PSCBase* psc);
};

class PSCEventBase : public PSCBase
{

protected:
    unsigned int mask;

    EventBase::pointer base;
    bufferevent *session;
public:
    PSCEventBase(const std::string& name,
                 const std::string& host,
                 unsigned short port,
                 unsigned int timeoutmask);
    virtual ~PSCEventBase();

};

class PSC : public PSCEventBase
{

    event *reconnect_timer;
    evdns_base *dns;
    bool timer_active;

public:
    PSC(const std::string& name,
        const std::string& host,
        unsigned short port,
        unsigned int timeoutmask);
    virtual ~PSC();

    virtual void flushSend() override;
    virtual void forceReConnect() override;

    virtual void report(int lvl) override;

private:
    void queueHeader(Block* blk, epicsUInt16 id, epicsUInt32 buflen);
public:
    virtual void queueSend(epicsUInt16, const void*, epicsUInt32) override final;
    virtual void queueSend(Block*, const dbuffer&) override final;
    virtual void queueSend(Block*, const void*, epicsUInt32) override final;
private:

    // RX message decoding
    bool have_head;
    epicsUInt16 header;
    epicsUInt32 bodylen;
    Block *bodyblock;
    size_t expect;

    evbuffer *sendbuf;

    void sendblock(Block*);

    virtual void connect() override;
    void start_reconnect();

    // libevent callbacks
    void eventcb(short);
    void recvdata();
    void reconnect();

public:
    // atexit
    void stop();
private:

    static void bev_eventcb(bufferevent*,short,void*);
    static void bev_datacb(bufferevent*, void*);
    static void bev_reconnect(int,short,void*);
};

class PSCUDP : public PSCEventBase
{
public:
    PSCUDP(const std::string& name,
           const std::string& host,
           unsigned short hostport,
           unsigned short ifaceport,
           unsigned int timeoutmask);
    virtual ~PSCUDP();

    virtual void queueSend(epicsUInt16, const void*, epicsUInt32) override final;
    virtual void queueSend(Block*, const dbuffer&) override final;
    virtual void queueSend(Block*, const void*, epicsUInt32) override final;

    virtual void flushSend() override;
    virtual void forceReConnect() override;

private:
    void queueHeader(Block* blk, epicsUInt16 id, epicsUInt32 buflen);

    sockaddr_in ep;

    int socket;
    event *evt_rx, *evt_tx;

    typedef std::vector<char> buffer_t;
    std::list<buffer_t> txqueue;
    buffer_t rxscratch;

    typedef std::list<buffer_t> sendbuf_t;
    sendbuf_t sendbuf, // pending flush
              txbuf;   // ready to sendto()
    sendbuf_t readybuf;// a free list

    virtual void connect() override;

    void senddata(short evt);
    void recvdata(short evt);

    static void ev_send(int,short,void*);
    static void ev_recv(int,short,void*);
};

#endif // PSC_H
