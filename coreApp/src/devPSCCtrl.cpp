/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include "psc/devcommon.h"

#include <stdexcept>
#include <algorithm>
#include <sstream>

#include <biRecord.h>
#include <boRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <stringinRecord.h>

recAlarm::recAlarm()
    :std::exception()
    ,status(COMM_ALARM)
    ,severity(INVALID_ALARM)
{}

recAlarm::recAlarm(short sts, short sevr)
    :std::exception()
    ,status(sts)
    ,severity(sevr)
{}

const char* recAlarm::what()
{
    return "Record alarm";
}

namespace {

long init_common(dbCommon* prec, const char*link)
{
    try {
        PSCBase *psc = PSCBase::getPSCBase(link);
        if(!psc) {
            timefprintf(stderr, "%s: can't find PSC '%s'\n", prec->name, link);
        }
        prec->dpvt = (void*)psc;
    }CATCH(init_common, prec)
    return 0;
}

template<class R>
long init_input(R* prec)
{
    assert(prec->inp.type==INST_IO);
    return init_common((dbCommon*)prec, prec->inp.value.instio.string);
}

template<class R>
long init_output(R* prec)
{
    assert(prec->out.type==INST_IO);
    return init_common((dbCommon*)prec, prec->out.value.instio.string);
}

long init_count(longinRecord* prec)
{
    assert(prec->inp.type==INST_IO);
    try {
        std::istringstream strm(prec->inp.value.instio.string);
        std::string pscname, direction;
        unsigned long blocknum;
        strm >> pscname >> blocknum >> direction;
        if(strm.fail())
            throw std::runtime_error("Failed to parse INP");
        PSCBase *psc = PSCBase::getPSCBase(pscname);
        if(!psc)
            throw std::runtime_error("Can't find PSC");
        Block *block=NULL;
        if(direction=="rx")
            block = psc->getRecv(blocknum);
        else if(direction=="tx")
            block = psc->getSend(blocknum);
        else
            throw std::runtime_error("Invalid direction");
        if(!block)
            throw std::runtime_error("Can't get PSC block");
        prec->dpvt = (void*)block;
    }CATCH(init_count, prec)
    return 0;
}

long get_iointr_info(int cmd, dbCommon *prec, IOSCANPVT *io)
{
    if(!prec->dpvt)
        return -1;
    PSC *psc=(PSC*)prec->dpvt;

    *io = psc->scan;
    return 0;
}

long read_bi_connected(biRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    PSC *psc=(PSC*)prec->dpvt;
    try {
        Guard g(psc->lock);

        prec->rval = psc->isConnected();
    }CATCH(read_bi_connected, prec)

    return 0;
}

long read_si_message(stringinRecord *prec)
{
    if(!prec->dpvt)
        return -1;
    PSC *psc=(PSC*)prec->dpvt;
    try {
        Guard g(psc->lock);

        size_t len = std::max(psc->message.size(), (size_t)MAX_STRING_SIZE);

        strncpy(prec->val, psc->message.c_str(), len);
        prec->val[MAX_STRING_SIZE-1] = '\0';
    }CATCH(read_si_message, prec)

    return 0;
}

long read_unknown_count(longinRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    PSC *psc=(PSC*)prec->dpvt;
    try {
        Guard g(psc->lock);
        prec->val = psc->getUnknownCount();
    }CATCH(read_si_message, prec)

    return 0;
}

long read_connection_count(longinRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    PSC *psc=(PSC*)prec->dpvt;
    try {
        Guard g(psc->lock);
        prec->val = psc->getConnCount();
    }CATCH(read_si_message, prec)

    return 0;
}

long read_block_count(longinRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Block *block=(Block*)prec->dpvt;
    try {
        Guard g(block->psc.lock);
        prec->val = block->count;
    }CATCH(read_block_count, prec)

    return 0;
}

long write_force_reconnect(boRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    PSC *psc=(PSC*)prec->dpvt;
    try {
        Guard g(psc->lock);
        psc->forceReConnect();
    }CATCH(write_force_reconnect, prec)

    return 0;
}

long write_bo_send_changed(boRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    PSC *psc=(PSC*)prec->dpvt;
    try {
        Guard g(psc->lock);

        if(!psc->isConnected())
            recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
        else
            psc->flushSend();

    }CATCH(write_bo_send_changed, prec)
    return 0;
}

long write_lo_send_block(longoutRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    PSC *psc=(PSC*)prec->dpvt;
    try {
        Guard g(psc->lock);

        if(prec->val < 0 || prec->val > 0xffff) {
            recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
        } else {
            psc->send(prec->val);
        }
    }CATCH(write_lo_send_block, prec)

    return 0;
}

MAKEDSET(bi, devPSCConnectedBi, &init_input<biRecord>, &get_iointr_info, &read_bi_connected);
MAKEDSET(stringin, devPSCMessageSI, &init_input<stringinRecord>, &get_iointr_info, &read_si_message);
MAKEDSET(bo, devPSCSendAllBo  , &init_output<boRecord>, NULL, &write_bo_send_changed);
MAKEDSET(bo, devPSCForceReConn, &init_output<boRecord>, NULL, &write_force_reconnect);
MAKEDSET(longout, devPSCSendLo, &init_output<longoutRecord>, NULL, &write_lo_send_block);
MAKEDSET(longin, devPSCUknCountLi, &init_input<longinRecord>, &get_iointr_info, &read_unknown_count);
MAKEDSET(longin, devPSCConnCountLi, &init_input<longinRecord>, &get_iointr_info, &read_connection_count);
MAKEDSET(longin, devPSCBlockCountLi, &init_count, NULL, &read_block_count);

} // namespace

#include <epicsExport.h>

epicsExportAddress(dset, devPSCConnectedBi);
epicsExportAddress(dset, devPSCMessageSI);
epicsExportAddress(dset, devPSCSendAllBo);
epicsExportAddress(dset, devPSCForceReConn);
epicsExportAddress(dset, devPSCSendLo);
epicsExportAddress(dset, devPSCUknCountLi);
epicsExportAddress(dset, devPSCConnCountLi);
epicsExportAddress(dset, devPSCBlockCountLi);
