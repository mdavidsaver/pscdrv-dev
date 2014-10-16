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
#include <biRecord.h>
#include <boRecord.h>
#include <mbbiRecord.h>
#include <mbboRecord.h>
#include <mbbiDirectRecord.h>
#include <mbboDirectRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <aiRecord.h>
#include <aoRecord.h>

#include <menuConvert.h>

namespace {

template<typename R> struct extra_init {static void op(R* prec){}};
template<> struct extra_init<mbboRecord>{
    static void op(mbboRecord *prec){ prec->mask <<= prec->shft; }
};
template<> struct extra_init<mbbiRecord>{
    static void op(mbbiRecord *prec){ prec->mask <<= prec->shft; }
};
template<> struct extra_init<mbboDirectRecord>{
    static void op(mbboDirectRecord *prec){ prec->mask <<= prec->shft; }
};
template<> struct extra_init<mbbiDirectRecord>{
    static void op(mbbiDirectRecord *prec){ prec->mask <<= prec->shft; }
};

template<typename R>
long init_input(R* prec)
{
    assert(prec->inp.type==INST_IO);
    extra_init<R>::op(prec);
    try {
        std::auto_ptr<Priv> priv(new Priv(prec));

        parse_link(priv.get(), prec->inp.value.instio.string, 0);

        prec->dpvt = (void*)priv.release();

    }CATCH(init_input, prec)
    return 0;
}

template<typename R>
long init_rb(R* prec)
{
    assert(prec->inp.type==INST_IO);
    extra_init<R>::op(prec);
    try {
        std::auto_ptr<Priv> priv(new Priv(prec));

        parse_link(priv.get(), prec->inp.value.instio.string, 1);

        prec->dpvt = (void*)priv.release();

    }CATCH(init_input, prec)
    return 0;
}

template<typename R>
long init_output(R* prec)
{
    assert(prec->out.type==INST_IO);
    extra_init<R>::op(prec);
    try {
        std::auto_ptr<Priv> priv(new Priv(prec));

        parse_link(priv.get(), prec->out.value.instio.string, 1);

        prec->dpvt = (void*)priv.release();

    }CATCH(init_output, prec)
    return 0;
}

long get_iointr_info(int cmd, dbCommon *prec, IOSCANPVT *io)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;

    *io = priv->block->scan;
    return 0;
}

template<typename T>
void read_to_field(dbCommon *prec, Priv *priv, T* pfield)
{
    if(priv->offset > priv->block->data.size()
            || (priv->block->data.size() - priv->offset) < sizeof(T))
    {
        timeprintf("%s: offset %d does not fit in block of size %d\n",
               prec->name, (int)priv->offset, (int)priv->block->data.size());
        throw recAlarm(SOFT_ALARM, INVALID_ALARM);
    }

    if(!priv->psc->isConnected()) {
        int junk = recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        junk += 1;
    }

    const T* pdata = (T*)&priv->block->data[priv->offset];

    *pfield = ntoh(*pdata);
    return;
}

// use for bi, mbbi, and mbbiDirect
template<typename R>
long read_binary(R *prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;
    try {
        Guard g(priv->psc->lock);

        epicsUInt32 temp = 0;
        read_to_field((dbCommon*)prec, priv, &temp);
        if(prec->mask)
            temp &= prec->mask;
        prec->rval = temp;

        setRecTimestamp(priv);
    }CATCH(read_binary, prec)
    return 0;
}

long read_li(longinRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;
    try {
        Guard g(priv->psc->lock);
        epicsInt32 v;

        read_to_field((dbCommon*)prec, priv, &v);
        prec->val = v;

        setRecTimestamp(priv);
    }CATCH(read_li, prec)
    return 0;
}

long read_ai(aiRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;
    try {
        Guard g(priv->psc->lock);
        epicsInt32 v;

        read_to_field((dbCommon*)prec, priv, &v);
        prec->rval = v;

        setRecTimestamp(priv);
    }CATCH(read_ai, prec)

    return 0;
}

template <typename T>
long read_ai_float(aiRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;
    T v;
    try {
        Guard g(priv->psc->lock);

        read_to_field((dbCommon*)prec, priv, &v);
        prec->val = analogRaw2EGU<double>(prec, v);
        prec->udf = isnan(prec->val);
        setRecTimestamp(priv);
    }CATCH(read_ai, prec)

    return 2;
}

template<typename T>
void write_from_field(dbCommon *prec, Priv *priv, const T* pfield)
{
    if(priv->offset > priv->block->data.size()
            || (priv->block->data.size() - priv->offset) < sizeof(T))
    {
        throw recAlarm(SOFT_ALARM, INVALID_ALARM);
    }

    if(!priv->psc->isConnected()) {
        int junk = recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
        junk += 1;
    }

    T *pdata = (T*)&priv->block->data[priv->offset];

    *pdata = hton(*pfield);
    return;
}

// used for bo, mbbo, and mbboDirect
template<typename R>
long write_binary(R *prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;
    try {
        Guard g(priv->psc->lock);

        epicsUInt32 temp = 0;

        if(prec->mask) {
            read_to_field((dbCommon*)prec, priv, &temp);
            temp &= ~prec->mask; // clear masked bits
            temp |= prec->rval & prec->mask;

        } else
            temp = prec->rval;

        write_from_field((dbCommon*)prec, priv, &temp);
    }CATCH(write_binary, prec)

    return 0;
}

long write_lo(longoutRecord *prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;
    try {
        Guard g(priv->psc->lock);
        epicsInt32 v = prec->val;

        write_from_field((dbCommon*)prec, priv, &v);
    }CATCH(write_lo, prec)
    return 0;
}

long write_ao(aoRecord *prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;
    try {
        Guard g(priv->psc->lock);
        epicsInt32 v = prec->rval;

        write_from_field((dbCommon*)prec, priv, &v);
    }CATCH(write_ao, prec)
    return 0;
}

template <typename T>
long write_ao_float(aoRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;
    try {
        Guard g(priv->psc->lock);
        T v = analogEGU2Raw<T>(prec, prec->val);
        write_from_field((dbCommon*)prec, priv, &v);
    }CATCH(write_ao, prec)
    return 0;
}

// Read data from PSC
MAKEDSET(bi, devPSCRegBi, &init_input<biRecord>, &get_iointr_info, &read_binary<biRecord>);
MAKEDSET(mbbi, devPSCRegMbbi, &init_input<mbbiRecord>, &get_iointr_info, &read_binary<mbbiRecord>);
MAKEDSET(mbbiDirect, devPSCRegMbbiDirect, &init_input<mbbiDirectRecord>, &get_iointr_info, &read_binary<mbbiDirectRecord>);
MAKEDSET(longin, devPSCRegLi, &init_input<longinRecord>, &get_iointr_info, &read_li);
MAKEDSET(ai, devPSCRegAi, &init_input<aiRecord>, &get_iointr_info, &read_ai);
MAKEDSET(ai, devPSCRegF32Ai, &init_input<aiRecord>, &get_iointr_info, &read_ai_float<float>);
MAKEDSET(ai, devPSCRegF64Ai, &init_input<aiRecord>, &get_iointr_info, &read_ai_float<double>);

// Echo back settings
MAKEDSET(bi, devPSCRegRBBi, &init_rb<biRecord>, NULL, &read_binary<biRecord>);
MAKEDSET(mbbi, devPSCRegRBMbbi, &init_rb<mbbiRecord>, NULL, &read_binary<mbbiRecord>);
MAKEDSET(mbbiDirect, devPSCRegRBMbbiDirect, &init_rb<mbbiDirectRecord>, NULL, &read_binary<mbbiDirectRecord>);
MAKEDSET(longin, devPSCRegRBLi, &init_rb<longinRecord>, NULL, &read_li);
MAKEDSET(ai, devPSCRegRBAi, &init_rb<aiRecord>, NULL, &read_ai);
MAKEDSET(ai, devPSCRegRBF32Ai, &init_rb<aiRecord>, NULL, &read_ai_float<float>);
MAKEDSET(ai, devPSCRegRBF64Ai, &init_rb<aiRecord>, NULL, &read_ai_float<double>);

// Update settings
MAKEDSET(bo, devPSCRegBo, &init_output<boRecord>, NULL, &write_binary<boRecord>);
MAKEDSET(mbbo, devPSCRegMbbo, &init_output<mbboRecord>, NULL, &write_binary<mbboRecord>);
MAKEDSET(mbboDirect, devPSCRegMbboDirect, &init_output<mbboDirectRecord>, NULL, &write_binary<mbboDirectRecord>);
MAKEDSET(longout, devPSCRegLo, &init_output<longoutRecord>, NULL, &write_lo);
MAKEDSET(ao, devPSCRegAo, &init_output<aoRecord>, NULL, &write_ao);
MAKEDSET(ao, devPSCRegF32Ao, &init_output<aoRecord>, NULL, &write_ao_float<float>);
MAKEDSET(ao, devPSCRegF64Ao, &init_output<aoRecord>, NULL, &write_ao_float<double>);

} // namespace

#include <epicsExport.h>

epicsExportAddress(dset, devPSCRegBi);
epicsExportAddress(dset, devPSCRegMbbi);
epicsExportAddress(dset, devPSCRegMbbiDirect);
epicsExportAddress(dset, devPSCRegLi);
epicsExportAddress(dset, devPSCRegAi);
epicsExportAddress(dset, devPSCRegF32Ai);
epicsExportAddress(dset, devPSCRegF64Ai);

epicsExportAddress(dset, devPSCRegRBBi);
epicsExportAddress(dset, devPSCRegRBMbbi);
epicsExportAddress(dset, devPSCRegRBMbbiDirect);
epicsExportAddress(dset, devPSCRegRBLi);
epicsExportAddress(dset, devPSCRegRBAi);
epicsExportAddress(dset, devPSCRegRBF32Ai);
epicsExportAddress(dset, devPSCRegRBF64Ai);

epicsExportAddress(dset, devPSCRegBo);
epicsExportAddress(dset, devPSCRegMbbo);
epicsExportAddress(dset, devPSCRegMbboDirect);
epicsExportAddress(dset, devPSCRegLo);
epicsExportAddress(dset, devPSCRegAo);
epicsExportAddress(dset, devPSCRegF32Ao);
epicsExportAddress(dset, devPSCRegF64Ao);
