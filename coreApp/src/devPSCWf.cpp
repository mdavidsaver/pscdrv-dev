/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <limits>

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

namespace {
template<size_t N> struct Int;
template<> struct Int<1u> { typedef epicsUInt8 type; };
template<> struct Int<2u> { typedef epicsUInt16 type; };
template<> struct Int<4u> { typedef epicsUInt32 type; };
template<> struct Int<8u> { typedef epicsUInt64 type; };
} // namespace

template<typename T, size_t I=sizeof(T)>
long read_wf_real(waveformRecord* prec)
{
    STATIC_ASSERT(I>0 && I<=sizeof(T));
    typedef typename detail::nswap<sizeof(T)>::utype store_type;

    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;

    // block buffer step size in bytes, default to element size
    const size_t istep = priv->step!=0 ? priv->step : I;
    const size_t iskip = istep>=I ? istep-I : 0u;
    // non-zero when widening integer
    const size_t oskip = sizeof(T)-I;

    store_type sign_mask = store_type(1u)<<(8u*I-1);
    store_type extend = 0;
    if(oskip)
        extend = std::numeric_limits<store_type>::max()<<(8u*I);

    try {
        Guard g(priv->psc->lock);

        if(!priv->psc->isConnected()) {
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            return 0;
        }

        store_type* out = (store_type*)prec->bptr;

        // HACK: we are copying integers into a double[]
        // this is safe so long as sizeof(T)<=sizeof(double)
        size_t nelem = priv->block->data.copyout_shape(oskip+(char*)out, priv->offset, I, iskip, oskip, prec->nelm);

        // step backwards since we are expanding the used size of the array
        for(size_t i=nelem; i; i--) {
            store_type raw = ntoh(out[i-1u]);
            if(oskip) {
                if(raw & sign_mask)
                    raw |= extend;
                else
                    raw &= ~extend;
            }

            ((double*)prec->bptr)[i-1u] = T(raw);
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
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            return 0;
        }

        size_t len = priv->block->data.copyout(prec->bptr, priv->offset, prec->nelm);

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

static dset6<waveformRecord> devPSCBlockInWf24 = {{6, NULL, NULL, &init_wf_record<0>, &get_iointr_info}, &read_wf_real<epicsInt32, 3u>, NULL};

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
epicsExportAddress(dset, devPSCBlockInWf24);
epicsExportAddress(dset, devPSCBlockInWf32);
epicsExportAddress(dset, devPSCBlockOutWf32);
epicsExportAddress(dset, devPSCBlockInWfF32);
epicsExportAddress(dset, devPSCBlockOutWfF32);
epicsExportAddress(dset, devPSCBlockInWfF64);
epicsExportAddress(dset, devPSCBlockOutWfF64);
