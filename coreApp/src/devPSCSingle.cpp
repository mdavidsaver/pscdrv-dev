/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include "psc/devcommon.h"

#include <stdio.h>

#include <osiSock.h>
#include <epicsMath.h>
#include <callback.h>

#include <boRecord.h>
#include <mbboRecord.h>
#include <mbboDirectRecord.h>
#include <longoutRecord.h>
#include <aoRecord.h>

#include <menuConvert.h>

namespace {

template<typename R> struct extra_init {static void op(R* prec){}};
template<> struct extra_init<mbboRecord>{
    static void op(mbboRecord *prec){ prec->mask <<= prec->shft; }
};
template<> struct extra_init<mbboDirectRecord>{
    static void op(mbboDirectRecord *prec){ prec->mask <<= prec->shft; }
};

struct SinglePriv : Priv
{
    Block::data_t syncData;

    CALLBACK syncCB;

    epicsUInt8 syncNow; // must host record lock to access

    template<class R>
    SinglePriv(R* pr) : Priv(pr), syncNow(false) {}
};

void received_block(void* raw, Block* block)
{
    SinglePriv *priv=(SinglePriv*)raw;
    union {
        epicsUInt32 addr;
        char bytes[sizeof(epicsUInt32)];
    } data;

    if(block->data.size()<8)
        return;

    memcpy(data.bytes, &priv->block->data[0], sizeof(data.bytes));

    // not for this record
    if(priv->offset!=ntohl(data.addr))
        return;

    bool alreadyQueued = !priv->syncData.empty();

    priv->syncData.resize(block->data.size());
    std::copy(priv->block->data.begin(),
              priv->block->data.end(),
              priv->syncData.begin());

    if(!alreadyQueued)
        callbackRequest(&priv->syncCB);
}

void sync_callback(CALLBACK *cb)
{
    SinglePriv *priv = (SinglePriv*)cb->user;

    //callbackGetUser(priv, cb);

    dbScanLock(priv->prec);
    priv->syncNow = true;
    dbProcess(priv->prec);
    priv->syncNow = false;
    dbScanUnlock(priv->prec);
}

template<typename R>
long init_output(R* prec)
{
    assert(prec->out.type==INST_IO);
    extra_init<R>::op(prec);
    try {
        std::auto_ptr<SinglePriv> priv(new SinglePriv(prec));

        RecInfo info((dbCommon*)prec);

        bool syncme = false;
        const char* sync = info.get("SYNC");
        if(sync && strcmp(sync, "SAME")==0)
            syncme = true;

        parse_link(priv.get(), prec->out.value.instio.string, syncme ? 0 : 2);

        callbackSetCallback(&sync_callback, &priv->syncCB);
        callbackSetPriority(priorityMedium, &priv->syncCB);
        callbackSetUser(priv.get(), &priv->syncCB);

        if(priv->block)
            priv->block->listeners.add(&received_block, (void*)priv.get());

        prec->dpvt = (void*)priv.release();

    }CATCH(init_output, prec)
    return 0;
}

template<typename T>
union msg {
    struct frame {
        epicsUInt32 addr;
        T val;
    } body;
    char bytes[sizeof(frame)];
};

template<typename T>
void write_msg(dbCommon *prec, Priv *priv, T value)
{
    msg<T> tosend;

    tosend.body.addr = hton<epicsUInt32>(priv->offset);
    tosend.body.val = hton(value);

    Guard g(priv->psc->lock);

    if(!priv->psc->isConnected())
        throw recAlarm(WRITE_ALARM, INVALID_ALARM);

    priv->psc->queueSend(priv->bid, tosend.bytes, sizeof(tosend.bytes));
}

template<typename T>
void read_msg(dbCommon *prec, SinglePriv *priv, T* value)
{
    msg<T> torecv;

    if(priv->block->data.size()<sizeof(torecv.bytes)) {
        errlogPrintf("%s: data too short to resync\n", priv->prec->name);
        return;
    }

    memcpy(torecv.bytes, &priv->syncData[0], sizeof(torecv.bytes));
    priv->syncData.clear();

    // already checked...
    assert(priv->offset==ntohl(torecv.body.addr));

    *value = ntoh(torecv.body.val);

    // forceably clear the UDF alarm computed from
    // previous value
    if(prec->nsta==UDF_ALARM && prec->nsev==INVALID_ALARM)
        prec->nsta = prec->nsev = 0;
}

template<typename T>
long write_msg_val(longoutRecord* prec) {
    if(!prec->dpvt)
        return -1;
    SinglePriv *priv=(SinglePriv*)prec->dpvt;

    try {
        if(!priv->syncNow)
            write_msg<T>((dbCommon*)prec, priv, prec->val);
        else
            read_msg<T>((dbCommon*)prec, priv, &prec->val);
        prec->udf = 0;
    }CATCH(write_msg_val, prec)

    return 0;
}

long write_msg_rval(aoRecord* prec) {
    if(!prec->dpvt)
        return -1;
    SinglePriv *priv=(SinglePriv*)prec->dpvt;

    try {
        if(!priv->syncNow)
            write_msg<epicsInt32>((dbCommon*)prec, priv, prec->rval);
        else {
            read_msg<epicsInt32>((dbCommon*)prec, priv, &prec->rval);

            prec->val = analogRaw2EGU<double>(prec, prec->rval);
            prec->udf = isnan(prec->val);
            if(prec->udf)
                (void)recGblSetSevr(prec, UDF_ALARM, INVALID_ALARM);
        }
    }CATCH(write_msg_rval, prec)

    return 0;
}

template<typename R>
long write_msg_binary_rval(R* prec) {
    if(!prec->dpvt)
        return -1;
    SinglePriv *priv=(SinglePriv*)prec->dpvt;

    try {
        if(!priv->syncNow) {
            epicsUInt32 val = prec->rval;
            if(prec->mask)
                val &= prec->mask;
            write_msg((dbCommon*)prec, priv, val);
        } else {
            epicsUInt32 val = 0;
            read_msg((dbCommon*)prec, priv, &val);
            prec->rval = val;
        }
    }CATCH(write_msg_binary_rval, prec)

    return 0;
}


template<typename T>
long write_msg_val_ao(aoRecord* prec) {
    if(!prec->dpvt)
        return -1;
    SinglePriv *priv=(SinglePriv*)prec->dpvt;

    try {
        if(!priv->syncNow) {
            T v = analogEGU2Raw<T>(prec, prec->val);
            write_msg<T>((dbCommon*)prec, priv, v);
        } else {
            T v = 0;
            read_msg<T>((dbCommon*)prec, priv, &v);
            prec->val = analogRaw2EGU<double>(prec, v);
            prec->udf = isnan(prec->val);
            if(prec->udf)
                (void)recGblSetSevr(prec, UDF_ALARM, INVALID_ALARM);
        }

    }CATCH(write_msg_val_ao, prec)

    return 0;
}

} // namespace

MAKEDSET(bo, devPSCSingleU32Bo, &init_output<boRecord>, NULL, &write_msg_binary_rval<boRecord>);

MAKEDSET(mbbo, devPSCSingleU32Mbbo, &init_output<mbboRecord>, NULL, &write_msg_binary_rval<mbboRecord>);

MAKEDSET(mbboDirect, devPSCSingleU32MbboDirect, &init_output<mbboDirectRecord>, NULL, &write_msg_binary_rval<mbboDirectRecord>);

MAKEDSET(longout, devPSCSingleS32Lo, &init_output<longoutRecord>, NULL, &write_msg_val<epicsInt32>);

MAKEDSET(ao, devPSCSingleS32Ao, &init_output<aoRecord>, NULL, &write_msg_rval);

MAKEDSET(ao, devPSCSingleF32Ao, &init_output<aoRecord>, NULL, &write_msg_val_ao<float>);

MAKEDSET(ao, devPSCSingleF64Ao, &init_output<aoRecord>, NULL, &write_msg_val_ao<double>);

#include <epicsExport.h>

epicsExportAddress(dset, devPSCSingleU32Bo);
epicsExportAddress(dset, devPSCSingleU32Mbbo);
epicsExportAddress(dset, devPSCSingleU32MbboDirect);
epicsExportAddress(dset, devPSCSingleS32Lo);
epicsExportAddress(dset, devPSCSingleS32Ao);
epicsExportAddress(dset, devPSCSingleF32Ao);
epicsExportAddress(dset, devPSCSingleF64Ao);
