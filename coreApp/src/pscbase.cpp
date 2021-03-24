/*************************************************************************\
* Copyright (c) 2015 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <drvSup.h>
#include <initHooks.h>

#include <stdexcept>
#include <memory>
#include <cstring>
#include <cerrno>
#include <cstdio>

#include <event2/thread.h>

#include <epicsExit.h>

#define epicsExportSharedSymbols
#include "psc/device.h"

#include "utilpvt.h"

int PSCDebug = 1;
int PSCInactivityTime = 5;
int PSCMaxSendBuffer = 1024 * 1024;

PSCBase::pscmap_t PSCBase::pscmap;

Block::Block(PSCBase *p, epicsUInt16 c)
    :psc(*p)
    ,code(c)
    ,queued(false)
    ,scan()
    ,scanBusy(0u)
    ,scanQueued(false)
    ,count(0u)
    ,scanCount(0u)
    ,scanOflow(0u)
{
    scanIoInit(&scan);
    scanIoSetComplete(scan, &Block::scanned, this);
}

void Block::requestScan()
{
    if(scanBusy) {
        // previous scanning in progress
        scanQueued = true;
        scanOflow++;

    } else {
        scanBusy = scanIoRequest(scan);
        scanCount++;
    }
}

void Block::scanned(void *usr, IOSCANPVT, int prio)
{
    Block *self = (Block*)usr;
    try {
        Guard G(self->psc.lock);

        assert(self->scanBusy & (1<<prio));
        self->scanBusy &= ~(1<<prio);

        if(!self->scanBusy && self->scanQueued) {
            // scan done, and next scan already queued
            self->scanQueued = false;
            self->requestScan();
        }

    }catch(std::exception& e){
        errlogPrintf("Error in Block::scanned %s : %s\n", self->psc.name.c_str(), e.what());
    }
}

PSCBase::PSCBase(const std::string &name,
                 const std::string &host,
                 unsigned short port)
    :name(name)
    ,host(host)
    ,port(port)
    ,connected(false)
    ,ukncount(0)
    ,conncount(0)
    ,message("Initialize")
{
    scanIoInit(&scan);
    scanIoInit(&onConnect);

    pscmap[name] = this;
}

PSCBase::~PSCBase()
{
    pscmap.erase(name);
}

Block* PSCBase::getSend(epicsUInt16 block)
{
    block_map::const_iterator it = send_blocks.find(block);
    if(it!=send_blocks.end())
        return it->second;
    psc::auto_ptr<Block> ret(new Block(this, block));
    send_blocks[block] = ret.get();
    return ret.release();
}

Block* PSCBase::getRecv(epicsUInt16 block)
{
    block_map::const_iterator it = recv_blocks.find(block);
    if(it!=recv_blocks.end())
        return it->second;
    psc::auto_ptr<Block> ret(new Block(this, block));
    recv_blocks[block] = ret.get();
    return ret.release();
}

/* queue the requested register block */
void PSCBase::send(epicsUInt16 bid)
{
    block_map::const_iterator it = send_blocks.find(bid);
    if(it==send_blocks.end())
        return;
    Block *block = it->second;

    queueSend(block, block->data);
}

void PSCBase::startAll()
{
    pscmap_t::const_iterator it, end=pscmap.end();
    for(it=pscmap.begin(); it!=end; ++it) {
        Guard g(it->second->lock);
        it->second->connect();
    }
}

void PSCBase::stopAll()
{
    pscmap_t trash;
    pscmap.swap(trash);
    while(!trash.empty()) {
        PSCBase* psc = trash.begin()->second;
        trash.erase(trash.begin());
        psc->stop();
        delete psc;
    }
}

PSCBase *PSCBase::getPSCBase(const std::string& name)
{
    pscmap_t::const_iterator it=pscmap.find(name);
    if(it==pscmap.end())
        return NULL;
    return it->second;
}

PSCEventBase::PSCEventBase(const std::string& name,
                           const std::string& host,
                           unsigned short port,
                           unsigned int timeoutmask)
    :PSCBase (name, host, port)
    ,mask(timeoutmask)
    ,base(EventBase::makeBase())
    ,session(NULL)
{}

PSCEventBase::~PSCEventBase() {}

void psc_real_exit(evutil_socket_t, short, void *raw)
{
    PSCEventBase *self = (PSCEventBase*)raw;
    // finally cleanup
    self->stopinloop();
}

void PSCEventBase::stop()
{
    // jump to the event loop worker also syncs
    event_base_once(base->get(), -1, EV_TIMEOUT, &psc_real_exit, this, 0);
    base->stop();
}

extern "C"
void createPSC(const char* name, const char* host, int port, int timeout)
{
    try{
        new PSC(name, host, port, timeout);
    }catch(std::exception& e){
        iocshSetError(1);
        timefprintf(stderr, "Failed to create PSC '%s': %s\n", name, e.what());
    }
}

extern "C"
void createPSCUDP(const char* name, const char* host, int hostport, int ifaceport)
{
    try{
        new PSCUDP(name, host, hostport, ifaceport, 0);
    }catch(std::exception& e){
        iocshSetError(1);
        timefprintf(stderr, "Failed to create PSCUDP '%s': %s\n", name, e.what());
    }
}

extern "C"
void setPSCSendBlockSize(const char* name, int bid, int size)
{
    try {
        PSCBase *psc = PSCBase::getPSCBase(name);
        if(!psc)
            throw std::runtime_error("Unknown PSC");
        Block *block = psc->getSend(bid);
        if(!block)
            throw std::runtime_error("Can't select PSC Block");
        block->data.resize(size);
        timefprintf(stderr, "Set PSC '%s' send block %d size to %lu bytes\n",
                name, bid, (unsigned long)block->data.size());
    }catch(std::exception& e){
        timefprintf(stderr, "Failed to set PSC '%s' send block %d size to %d bytes: %s\n",
                name, bid, size, e.what());
    }
}

static void PSCAtExit(void*)
{
    PSCBase::stopAll();
}

static void PSCHook(initHookState state)
{
    if(state!=initHookAfterIocRunning)
        return;
    epicsAtExit(PSCAtExit, 0);
    PSCBase::startAll();
}

static
bool pscreportblock(int lvl, Block* block)
{
    printf(" Block %d\n", block->code);
    printf("  Queued : %s\n", block->queued  ? "Yes":"No");
    printf("  IOCount: %u  Size: %lu  ScanCount: %u  ScanOFlow: %u\n", block->count,
           (unsigned long)block->data.size(),
           (unsigned)block->scanCount,
           (unsigned)block->scanOflow);
    return true;
}

bool PSCBase::ReportOne(int lvl, PSCBase* psc)
{
    printf("PSC %s : %s:%d\n", psc->name.c_str(), psc->host.c_str(), psc->port);
    if(lvl<=0)
        return true;
    Guard G(psc->lock);
    printf(" Connected: %s\n", psc->isConnected() ? "Yes":"No");
    printf(" Conn Cnt : %u\n", (unsigned)psc->getConnCount());
    printf(" Unkn Cnt : %u\n", (unsigned)psc->getUnknownCount());
    psc->report(lvl);
    if(lvl>=2) {
        block_map::const_iterator it, end;
        printf(" Send blocks\n");
        for(it=psc->send_blocks.begin(), end=psc->send_blocks.end(); it!=end; ++it) {
            pscreportblock(lvl, it->second);
        }
        printf(" Recv blocks\n");
        for(it=psc->recv_blocks.begin(), end=psc->recv_blocks.end(); it!=end; ++it) {
            pscreportblock(lvl, it->second);
        }
        printf(" procOnConnect #%lu\n", psc->procOnConnect.size());
        if(lvl>=3) {
            for(size_t i=0, N=psc->procOnConnect.size(); i<N; i++) {
                printf("   %s\n", psc->procOnConnect[i]->name);
            }
        }
    }
    return true;
}

static
long pscreport(int level)
{
    PSCBase::visit(PSCBase::ReportOne, level);
    return 0;
}

void PSCBase::report(int){}

#include <iocsh.h>

static const iocshArg createPSCArg0 = {"name", iocshArgString};
static const iocshArg createPSCArg1 = {"hostname", iocshArgString};
static const iocshArg createPSCArg2 = {"port#", iocshArgInt};
static const iocshArg createPSCArg3 = {"enable recv timeout", iocshArgInt};
static const iocshArg * const createPSCArgs[] =
{&createPSCArg0,&createPSCArg1,&createPSCArg2,&createPSCArg3};
static const iocshFuncDef createPSCDef = {"createPSC", 4, createPSCArgs};
static void createPSCArgsCallFunc(const iocshArgBuf *args)
{
    createPSC(args[0].sval, args[1].sval, args[2].ival, args[3].ival);
}

static const iocshArg createPSCUDPArg0 = {"name", iocshArgString};
static const iocshArg createPSCUDPArg1 = {"hostname", iocshArgString};
static const iocshArg createPSCUDPArg2 = {"hostport#", iocshArgInt};
static const iocshArg createPSCUDPArg3 = {"ifaceport#", iocshArgInt};
static const iocshArg * const createPSCUDPArgs[] =
{&createPSCUDPArg0,&createPSCUDPArg1,&createPSCUDPArg2,&createPSCUDPArg3};
static const iocshFuncDef createPSCUDPDef = {"createPSCUDP", 4, createPSCUDPArgs};
static void createPSCUDPArgsCallFunc(const iocshArgBuf *args)
{
    createPSCUDP(args[0].sval, args[1].sval, args[2].ival, args[3].ival);
}

static const iocshArg setPSCArg0 = {"name", iocshArgString};
static const iocshArg setPSCArg1 = {"block", iocshArgInt};
static const iocshArg setPSCArg2 = {"size", iocshArgInt};
static const iocshArg * const setPSCArgs[] = {&setPSCArg0,&setPSCArg1,&setPSCArg2};
static const iocshFuncDef setPSCDef = {"setPSCSendBlockSize", 3, setPSCArgs};
static void setPSCCallFunc(const iocshArgBuf *args)
{
    setPSCSendBlockSize(args[0].sval, args[1].ival, args[2].ival);
}

static void PSCRegister(void)
{
    int ret =
#if defined(EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED)
        evthread_use_windows_threads();
#elif defined(EVTHREAD_USE_PTHREADS_IMPLEMENTED)
        evthread_use_pthreads();
#else
        1;
#error libevent threading not support for this target
#endif
    if(ret!=0) {
        timefprintf(stderr, "Failed to initialize libevent threading!.  PSC driver not loaded.\n");
        return;
    }
    iocshRegister(&createPSCDef, &createPSCArgsCallFunc);
    iocshRegister(&createPSCUDPDef, &createPSCUDPArgsCallFunc);
    iocshRegister(&setPSCDef, &setPSCCallFunc);
    initHookRegister(&PSCHook);
}

static
drvet drvPSC = {
    2,
    (DRVSUPFUN)pscreport,
    NULL
};

#include <epicsExport.h>
extern "C" {
epicsExportAddress(int, PSCDebug);
epicsExportAddress(int, PSCMaxSendBuffer);
epicsExportAddress(int, PSCInactivityTime);
epicsExportAddress(drvet, drvPSC);
epicsExportRegistrar(PSCRegister);
}
