/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <stdio.h>
#include <arpa/inet.h>

#include <menuFtype.h>
#include <waveformRecord.h>

#define epicsExportSharedSymbols
#include "psc/devcommon.h"

#include "utilpvt.h"

namespace {

template<int dir>
long init_wf_record(waveformRecord* prec)
{
    assert(prec->inp.type==INST_IO);
    if(prec->ftvl!=menuFtypeDOUBLE) {
        timefprintf(stderr, "%s: FTVL must be DOUBLE\n", prec->name);
        return 0;
    }
    try {
        psc::auto_ptr<Priv> priv(new Priv(prec));

        parse_link(priv.get(), prec->inp.value.instio.string, dir);

        prec->dpvt = (void*)priv.release();

    }CATCH(init_wf_record, prec)
return 0;
}

template<int dir>
long init_wf_record_bytes(waveformRecord* prec)
{
    assert(prec->inp.type==INST_IO);
    if(!(prec->ftvl==menuFtypeCHAR || prec->ftvl==menuFtypeUCHAR)) {
        timefprintf(stderr, "%s: FTVL must be CHAR or UCHAR\n", prec->name);
        return 0;
    }
    try {
        psc::auto_ptr<Priv> priv(new Priv(prec));

        parse_link(priv.get(), prec->inp.value.instio.string, dir);

        prec->dpvt = (void*)priv.release();

    }CATCH(init_wf_record, prec)
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
long read_wf_real(waveformRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;

    try {
        Guard g(priv->psc->lock);

        if(!priv->psc->isConnected()) {
            int junk = recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            junk += 1;
            return 0;
        }

        // source step size in bytes, default to element size
        const size_t step = priv->step!=0 ? priv->step : sizeof(T);
        const size_t skip = step>=sizeof(T) ? step-sizeof(T) : 0u;

        // HACK: we are copying integers into a double[]
        // this is safe so long as sizeof(T)<=sizeof(double)
        size_t ncopied = priv->block->data.copyout_shape(prec->bptr, priv->offset, sizeof(T), skip, prec->nelm);
        size_t nelem = ncopied;

        // step backwards since we are expanding the used size of the array
        for(size_t i=nelem; i; i--) {
            T raw = ((T*)prec->bptr)[i-1u];

            ((double*)prec->bptr)[i-1u] = ntoh(raw);
        }

        prec->nord = nelem;

        setRecTimestamp(priv);
    }CATCH(read_wf, prec)

    return 0;
}

long read_wf_bytes(waveformRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;

    try {
        Guard g(priv->psc->lock);

        if(!priv->psc->isConnected()) {
            int junk = recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            junk += 1;
            return 0;
        }

        size_t len = priv->block->data.copyout_shape(prec->bptr, priv->offset, prec->nelm, 0u, 1u);

        prec->nord = len;

        setRecTimestamp(priv);
    }CATCH(read_wf_bytes, prec)

    return 0;
}

template<typename T>
long write_wf(waveformRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;
    try {

        size_t len = prec->nord;

        double *pfrom = (double*)prec->bptr;
        std::vector<T> to(len);

        for(size_t i=0; i<len; i++) {
            T ival = pfrom[i];
            // endian fix and cast to unsigned
            to[i] = hton(ival);
        }

        Guard g(priv->psc->lock);

        if(!priv->psc->isConnected()) {
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            return 0;
        }

        priv->psc->queueSend(priv->block, &to[0], sizeof(T)*to.size());
    }CATCH(write_wf, prec)

    return 0;
}

long write_wf_bytes(waveformRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;
    try {
        Guard g(priv->psc->lock);

        if(!priv->psc->isConnected()) {
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            return 0;
        }

        priv->psc->queueSend(priv->block, prec->bptr, prec->nord);
    }CATCH(write_wf_bytes, prec)

    return 0;
}

MAKEDSET(waveform, devPSCBlockInWf8, &init_wf_record_bytes<0>, &get_iointr_info, &read_wf_bytes);
MAKEDSET(waveform, devPSCBlockOutWf8, &init_wf_record_bytes<1>, &get_iointr_info, &write_wf_bytes);

MAKEDSET(waveform, devPSCBlockInWf16, &init_wf_record<0>, &get_iointr_info, &read_wf_real<epicsInt16>);
MAKEDSET(waveform, devPSCBlockOutWf16, &init_wf_record<1>, &get_iointr_info, &write_wf<epicsInt16>);

MAKEDSET(waveform, devPSCBlockInWf32, &init_wf_record<0>, &get_iointr_info, &read_wf_real<epicsInt32>);
MAKEDSET(waveform, devPSCBlockOutWf32, &init_wf_record<1>, &get_iointr_info, &write_wf<epicsInt32>);

MAKEDSET(waveform, devPSCBlockInWfF32, &init_wf_record<0>, &get_iointr_info, &read_wf_real<float>);
MAKEDSET(waveform, devPSCBlockOutWfF32, &init_wf_record<1>, &get_iointr_info, &write_wf<float>);

MAKEDSET(waveform, devPSCBlockInWfF64, &init_wf_record<0>, &get_iointr_info, &read_wf_real<double>);
MAKEDSET(waveform, devPSCBlockOutWfF64, &init_wf_record<1>, &get_iointr_info, &write_wf<double>);

} // namespace

#include <epicsExport.h>

epicsExportAddress(dset, devPSCBlockInWf8);
epicsExportAddress(dset, devPSCBlockOutWf8);
epicsExportAddress(dset, devPSCBlockInWf16);
epicsExportAddress(dset, devPSCBlockOutWf16);
epicsExportAddress(dset, devPSCBlockInWf32);
epicsExportAddress(dset, devPSCBlockOutWf32);
epicsExportAddress(dset, devPSCBlockInWfF32);
epicsExportAddress(dset, devPSCBlockOutWfF32);
epicsExportAddress(dset, devPSCBlockInWfF64);
epicsExportAddress(dset, devPSCBlockOutWfF64);
