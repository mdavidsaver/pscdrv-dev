/*************************************************************************\
* Copyright (c) 2015 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include "psc/device.h"

#include <drvSup.h>
#include <initHooks.h>

#include <stdexcept>
#include <memory>
#include <cstring>
#include <cerrno>
#include <cstdio>

#include <event2/thread.h>

int PSCDebug = 1;
int PSCInactivityTime = 5;
int PSCMaxSendBuffer = 1024 * 1024;

PSCBase::pscmap_t PSCBase::pscmap;

Block::Block(PSCBase *p, epicsUInt16 c)
    :psc(*p)
    ,code(c)
    ,data()
    ,queued(false)
    ,scan()
{
    scanIoInit(&scan);
}


PSCBase::PSCBase(const std::string &name,
                 const std::string &host,
                 unsigned short port,
                 unsigned int timeoutmask)
    :name(name)
    ,host(host)
    ,port(port)
    ,mask(timeoutmask)
    ,session(NULL)
    ,connected(false)
    ,ukncount(0)
    ,conncount(0)
    ,sendbuf(evbuffer_new())
    ,message("Initialize")
{

}

PSCBase::~PSCBase()
{
    evbuffer_free(sendbuf);
}

Block* PSCBase::getSend(epicsUInt16 block)
{
    block_map::const_iterator it = send_blocks.find(block);
    if(it!=send_blocks.end())
        return it->second;
    std::auto_ptr<Block> ret(new Block(this, block));
    send_blocks[block] = ret.get();
    return ret.release();
}

Block* PSCBase::getRecv(epicsUInt16 block)
{
    block_map::const_iterator it = recv_blocks.find(block);
    if(it!=recv_blocks.end())
        return it->second;
    std::auto_ptr<Block> ret(new Block(this, block));
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

    queueSend(block, &block->data[0], block->data.size());
}

/* add a new message to the send queue */
void PSCBase::queueSend(epicsUInt16 id, const void* buf, epicsUInt32 buflen)
{
    Block *blk = getSend(id);
    queueSend(blk, buf, buflen);
}

void PSCBase::startAll()
{
    pscmap_t::const_iterator it, end=pscmap.end();
    for(it=pscmap.begin(); it!=end; ++it) {
        Guard g(it->second->lock);
        it->second->connect();
    }
}

PSCBase *PSCBase::getPSCBase(const std::string& name)
{
    pscmap_t::const_iterator it=pscmap.find(name);
    if(it==pscmap.end())
        return NULL;
    return it->second;
}

extern "C"
void createPSC(const char* name, const char* host, int port, int timeout)
{
    try{
        new PSC(name, host, port, timeout);
    }catch(std::exception& e){
        timefprintf(stderr, "Failed to create PSC '%s': %s\n", name, e.what());
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
        block->data.resize(size, 0);
        timefprintf(stderr, "Set PSC '%s' send block %d size to %lu bytes\n",
                name, bid, (unsigned long)block->data.size());
    }catch(std::exception& e){
        timefprintf(stderr, "Failed to set PSC '%s' send block %d size to %d bytes: %s\n",
                name, bid, size, e.what());
    }
}

static void PSCHook(initHookState state)
{
    if(state!=initHookAfterIocRunning)
        return;
    PSC::startAll();
}

static
bool pscreportone(int lvl, PSCBase* psc)
{
    psc->report(lvl);
    return true;
}

static
long pscreport(int level)
{
    PSCBase::visit(pscreportone, level);
    return 0;
}

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
#if defined(WIN32)
        evthread_use_windows_threads();
#elif defined(_EVENT_HAVE_PTHREADS)
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
