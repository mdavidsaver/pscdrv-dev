/*************************************************************************\
* Copyright (c) 2021 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <epicsString.h>
#include <epicsAtomic.h>

#include <lsoRecord.h>
#include <lsiRecord.h>
#include <boRecord.h>
#include <biRecord.h>
#include <aiRecord.h>
#include <longoutRecord.h>
#include <longinRecord.h>
#include <int64inRecord.h>
#include <aaiRecord.h>
#include <menuFtype.h>

#include "psc/devcommon.h"
#include "udpdrv.h"
#include "utilpvt.h"

#include <epicsExport.h>

namespace {

long devudp_init_record_period(aiRecord *prec)
{
    epicsTimeStamp *pvt = (epicsTimeStamp*)malloc(sizeof(*pvt));
    if(pvt)
        epicsTimeGetCurrent(pvt);
    prec->dpvt = pvt;
    return 0;
}

long devudp_interval(aiRecord *prec)
{
    epicsTimeStamp *pvt = (epicsTimeStamp*)prec->dpvt;
    epicsTimeStamp now;
    if(pvt && epicsTimeGetCurrent(&now)==0) {
        prec->val = epicsTimeDiffInSeconds(&now, pvt);
        *pvt = now;
        return 2;

    } else {
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        prec->val = 0.0;
        return 2;
    }
}

template<typename REC>
long devudp_init_record_out(REC *prec)
{
    try {
        DBLINK* plink = &prec->out;
        UDPFast* dev = PSCBase::getPSC<UDPFast>(plink->value.instio.string);
        if(!dev) {
            timefprintf(stderr, "%s: can't find UDPFast '%s'\n", prec->name, plink->value.instio.string);
        }
        prec->dpvt = (void*)dev;
        return 0;
    }CATCH(devudp_init_record, prec);
}

template<typename REC>
long devudp_init_record_in(REC *prec)
{
    try {
        DBLINK* plink = &prec->inp;
        UDPFast* dev = PSCBase::getPSC<UDPFast>(plink->value.instio.string);
        if(!dev) {
            timefprintf(stderr, "%s: can't find UDPFast '%s'\n", prec->name, plink->value.instio.string);
        }
        prec->dpvt = (void*)dev;
        return 0;
    }CATCH(devudp_init_record, prec);
}

#define TRY if(!prec->dpvt) return -1; UDPFast *dev = (UDPFast*)prec->dpvt; (void)dev; try

template<std::string UDPFast::*STR>
long devudp_set_string(lsoRecord* prec)
{
    TRY {
        std::string& str = dev->*STR;

        prec->val[prec->sizv - 1u] = '\0'; // paranoia
        std::string newv(prec->val);

        Guard G(dev->lock);
        str = newv;

        return 0;
    }CATCH(devudp_set_string, prec);
}

long devudp_reopen(boRecord* prec)
{
    TRY {
        {
            Guard G(dev->lock);
            dev->reopen = true;
        }
        dev->pendingReady.signal();
        return 0;
    }CATCH(devudp_reopen, prec);
}

long devudp_set_record(boRecord* prec)
{
    TRY {
        {
            Guard G(dev->lock);
            dev->record = prec->val;
            dev->reopen = dev->record;
        }
        if(prec->val)
            dev->pendingReady.signal();
        return 0;
    }CATCH(devudp_set_record, prec);
}

long devudp_get_record(biRecord* prec)
{
    TRY {
        Guard G(dev->lock);
        prec->rval = dev->record;
        return 0;
    }CATCH(devudp_get_record, prec);
}

long devudp_set_shortLimit(longoutRecord *prec)
{
    TRY {
        Guard G(dev->lock);
        if(prec->val>=0)
            dev->shortLimit = prec->val;
        prec->val = dev->shortLimit;
        return 0;
    }CATCH(devudp_get_record, prec);
}

template<const std::string UDPFast::*STR>
long devudp_get_string(lsiRecord* prec)
{
    TRY {
        const std::string& str = dev->*STR;

        Guard G(dev->lock);

        size_t s = str.size();
        if(s>=prec->sizv) { // truncating
            s = prec->sizv-1u;
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        }
        memcpy(prec->val, str.c_str(), s);
        prec->val[s] = '\0';
        prec->len = s+1u;
        return 0;
    }CATCH(devudp_get_string, prec);
}

long devudp_get_vpool(aiRecord* prec)
{
    TRY {
        double val;
        {
            Guard G(dev->lock);
            val = dev->vpool.size()/double(dev->vpoolTotal);
        }
        prec->val = analogRaw2EGU<double>(prec, val);
        return 2;
    }CATCH(devudp_get_vpool, prec);
}

long devudp_get_pending(aiRecord* prec)
{
    TRY {
        double val;
        {
            Guard G(dev->lock);
            val = dev->pending.size()/double(dev->vpoolTotal);
        }
        prec->val = analogRaw2EGU<double>(prec, val);
        return 2;
    }CATCH(devudp_get_pending, prec);
}

long devudp_get_inprog(aiRecord* prec)
{
    TRY {
        double val;
        {
            Guard G(dev->lock);
            epicsInt64 total = dev->vpoolTotal,
                       inuse = dev->vpool.size() + dev->pending.size();
            val = (total-inuse)/double(total);
        }
        prec->val = analogRaw2EGU<double>(prec, val);
        return 2;
    }CATCH(devudp_get_inprog, prec);
}

template<size_t UDPFast::*CNT>
long devudp_get_counter(int64inRecord *prec)
{
    TRY {
        prec->val = epicsAtomicGetSizeT(&(dev->*CNT));
        return 0;
    }CATCH(devudp_get_ ## NAME, prec);
}

long devudp_get_lastsize(int64inRecord* prec)
{
    TRY {
        prec->val = epicsAtomicGetSizeT(&dev->lastsize)/(1u<<20u);
        return 0;
    }CATCH(devudp_get_lastsize, prec);
}

struct privShortBuf {
    UDPFast *psc;
    unsigned int block;
    long offset;
    long step;
};

long devudp_clear_shortbuf(longinRecord *prec)
{
    TRY {
        UDPFast::pkts_t temp;
        {
            Guard S(dev->shortLock);
            temp.swap(dev->shortBuf);
        }
        bool unstall;
        {
            Guard G(dev->rxLock);
            unstall = dev->vpool.empty();
            for(size_t i=0u, N=temp.size(); i<N; i++) {
                dev->vpool.push_back(UDPFast::vecs_t::value_type());
                dev->vpool.back().swap(temp[i].body);
                assert(!dev->vpool.back().empty());
            }
            unstall &= !dev->vpool.empty();
        }
        if(unstall)
            dev->vpoolStall.signal();
        prec->val += (epicsInt32)temp.size();
        return 0;

    }CATCH(devudp_clear_shortbuf, prec);
}

#undef TRY
#define TRY if(!prec->dpvt) return -1; privShortBuf *priv = (privShortBuf*)prec->dpvt; (void)priv; try

template<typename R>
long devudp_init_record_shortbuf(R* prec)
{
    try {
        const char *link = prec->inp.value.instio.string;
        std::istringstream strm(link);
        std::string name;
        unsigned int block;
        long offset = 0;
        long step = 0;

        strm >> name >> block;
        if(!strm.eof())
            strm >> offset;
        if(!strm.eof())
            strm >> step;

        if(strm.fail()) {
            timefprintf(stderr, "%s: Error Parsing: '%s'\n",
                    prec->name, link);
            throw std::runtime_error("Link parsing error");
        } else if(!strm.eof()) {
            timefprintf(stderr, "%s: link parsing found \'%s\' instead of EOS\n",
                    prec->name, strm.str().substr(strm.tellg()).c_str());
        }

        psc::auto_ptr<privShortBuf> priv(new privShortBuf);
        priv->psc = PSC::getPSC<UDPFast>(name);
        priv->block = block;
        priv->offset = offset;
        priv->step = step;

        prec->dpvt = (void*)priv.release();
        return 0;

    }CATCH(devudp_init_record_shortbuf, prec);
}

long devudp_read_shortbuf_U32(aaiRecord* prec)
{
    if(prec->ftvl!=menuFtypeULONG) {
        (void)recGblSetSevr(prec, STATE_ALARM, INVALID_ALARM);
        return 0;
    }

    TRY {
        Guard S(priv->psc->shortLock);

        if(!priv->psc->isConnected()) {
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        }

        if(priv->psc->shortLimit < prec->nelm) {
            // automatically expand
            priv->psc->shortLimit = prec->nelm;
        }

        size_t N = std::min(size_t(prec->nelm), priv->psc->shortBuf.size());
        epicsUInt32* arr = static_cast<epicsUInt32*>(prec->bptr);
        epicsUInt64 reftime; // ns

        bool first = true;
        for(size_t i=0; i<N; i++) {
            const UDPFast::pkt& pkt = priv->psc->shortBuf[i];
            if(pkt.msgid != priv->block)
                continue;

            if(first) {
                if(prec->tse==epicsTimeEventDeviceTime)
                    prec->time = pkt.rxtime;

                reftime = pkt.rxtime.secPastEpoch;
                reftime *= 1000000000u;
                reftime += pkt.rxtime.nsec;

                first = false;
            }

            if(priv->offset < 0u) {
                // magic offset to access time
                epicsUInt64 curtime = pkt.rxtime.secPastEpoch;
                curtime *= 1000000000u;
                curtime += pkt.rxtime.nsec;

                arr[i] = curtime - reftime;

            } else if(priv->offset < 0u || size_t(priv->offset) + 4u > pkt.body.size()) {
                (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);

            } else {
                epicsUInt32 rval = 0u;
                memcpy(&rval, &pkt.body[priv->offset], 4u);
                arr[i] = ntohl(rval);
            }

        }

        prec->nord = epicsUInt32(N);
        return 0;

    }CATCH(devudp_read_shortbuf_U32, prec);
}

long devudp_read_shortbuf_I24_packed(aaiRecord* prec)
{
    if(prec->ftvl!=menuFtypeLONG) {
        (void)recGblSetSevr(prec, STATE_ALARM, INVALID_ALARM);
        return 0;
    }

    TRY {
        Guard S(priv->psc->shortLock);

        if(!priv->psc->isConnected()) {
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        }

        size_t Iout = 0u;
        size_t Nout = size_t(prec->nelm);
        epicsInt32* out = static_cast<epicsInt32*>(prec->bptr);

        // loop through available packets
        size_t skips = 0u;
        for(size_t i=0u, N=priv->psc->shortBuf.size(); i<N && Iout<Nout; i++) {
            const UDPFast::pkt& pkt = priv->psc->shortBuf[i];
            if(pkt.msgid != priv->block) {
                skips++;
                continue;
            }

            size_t pos = priv->offset;
            size_t step = priv->step;
            size_t limit = std::min(pkt.bodylen, pkt.body.size());

            // loop through samples in a packet
            while(pos+3u <= limit && Iout<Nout) {
                union {
                    epicsUInt32 I;
                    char B[4];
                } pun;
                memcpy(&pun.B[1], &pkt.body[pos], 3u);
                // sign extend...
                // B[0] = 0xff if B[1]&0x80 else 0x00
                pun.B[0] = pun.B[1]&0x80;
                pun.B[0] |= pun.B[0]>>1u;
                pun.B[0] |= pun.B[0]>>2u;
                pun.B[0] |= pun.B[0]>>4u;

                out[Iout++] = ntohl(pun.I);

                pos += step;
            }
        }

        prec->nord = Iout;

        if(priv->psc->shortBuf.size() >= priv->psc->shortLimit && Iout < Nout && !skips) {
            // short buf. filled, but this record had space remaining.
            // increase short buf limit for the next iteration.
            // iff no other packet t
            priv->psc->shortLimit++;

        }

        return 0;
    }CATCH(devudp_read_shortbuf_I24, prec);
}

#undef TRY

MAKEDSET(ai, devPSCUDPIntervalAI, &devudp_init_record_period, 0, &devudp_interval);
MAKEDSET(lso, devPSCUDPFilebaseLSO, &devudp_init_record_out, 0, &devudp_set_string<&UDPFast::filebase>);
MAKEDSET(lso, devPSCUDPFiledirLSO, &devudp_init_record_out, 0, &devudp_set_string<&UDPFast::filedir>);
MAKEDSET(bo, devPSCUDPReopenBO, &devudp_init_record_out, 0, &devudp_reopen);
MAKEDSET(bo, devPSCUDPRecordBO, &devudp_init_record_out, 0, &devudp_set_record);
MAKEDSET(bi, devPSCUDPRecordBI, &devudp_init_record_in, 0, &devudp_get_record);
MAKEDSET(longout, devPSCUDPShortLimitLO, &devudp_init_record_out, 0, &devudp_set_shortLimit);
MAKEDSET(lsi, devPSCUDPFilenameLSI, &devudp_init_record_in, 0, &devudp_get_string<&UDPFast::lastfile>);
MAKEDSET(lsi, devPSCUDPErrorLSI, &devudp_init_record_in, 0, &devudp_get_string<&UDPFast::lasterror>);
MAKEDSET(ai, devPSCUDPvpoolAI, &devudp_init_record_in, 0, &devudp_get_vpool);
MAKEDSET(ai, devPSCUDPpendingAI, &devudp_init_record_in, 0, &devudp_get_pending);
MAKEDSET(ai, devPSCUDPinprogAI, &devudp_init_record_in, 0, &devudp_get_inprog);
MAKEDSET(int64in, devPSCUDPnetrxI64I, &devudp_init_record_in, 0, &devudp_get_counter<&UDPFast::netrx>);
MAKEDSET(int64in, devPSCUDPwroteI64I, &devudp_init_record_in, 0, &devudp_get_counter<&UDPFast::storewrote>);
MAKEDSET(int64in, devPSCUDPndropI64I, &devudp_init_record_in, 0, &devudp_get_counter<&UDPFast::ndrops>);
MAKEDSET(int64in, devPSCUDPnignoreI64I, &devudp_init_record_in, 0, &devudp_get_counter<&UDPFast::nignore>);
MAKEDSET(int64in, devPSCUDPlastsizeI64I, &devudp_init_record_in, 0, &devudp_get_lastsize);
MAKEDSET(int64in, devPSCUDPnrxI64I, &devudp_init_record_in, 0, &devudp_get_counter<&UDPFast::rxcnt>);
MAKEDSET(int64in, devPSCUDPntimeoutI64I, &devudp_init_record_in, 0, &devudp_get_counter<&UDPFast::ntimeout>);
MAKEDSET(int64in, devPSCUDPnoomI64I, &devudp_init_record_in, 0, &devudp_get_counter<&UDPFast::noom>);
MAKEDSET(longin, devPSCUDPShortClearLI, &devudp_init_record_in, 0, &devudp_clear_shortbuf);
MAKEDSET(aai, devPSCUDPShortGetAAI, &devudp_init_record_shortbuf, 0, &devudp_read_shortbuf_U32);
MAKEDSET(aai, devPSCUDPShortGetI24AAI, &devudp_init_record_shortbuf, 0, &devudp_read_shortbuf_I24_packed);

}

extern "C" {
epicsExportAddress(dset, devPSCUDPIntervalAI);
epicsExportAddress(dset, devPSCUDPFilebaseLSO);
epicsExportAddress(dset, devPSCUDPFiledirLSO);
epicsExportAddress(dset, devPSCUDPReopenBO);
epicsExportAddress(dset, devPSCUDPRecordBO);
epicsExportAddress(dset, devPSCUDPRecordBI);
epicsExportAddress(dset, devPSCUDPShortLimitLO);
epicsExportAddress(dset, devPSCUDPFilenameLSI);
epicsExportAddress(dset, devPSCUDPErrorLSI);
epicsExportAddress(dset, devPSCUDPvpoolAI);
epicsExportAddress(dset, devPSCUDPpendingAI);
epicsExportAddress(dset, devPSCUDPinprogAI);
epicsExportAddress(dset, devPSCUDPnetrxI64I);
epicsExportAddress(dset, devPSCUDPwroteI64I);
epicsExportAddress(dset, devPSCUDPndropI64I);
epicsExportAddress(dset, devPSCUDPnignoreI64I);
epicsExportAddress(dset, devPSCUDPlastsizeI64I);
epicsExportAddress(dset, devPSCUDPnrxI64I);
epicsExportAddress(dset, devPSCUDPntimeoutI64I);
epicsExportAddress(dset, devPSCUDPnoomI64I);
epicsExportAddress(dset, devPSCUDPShortClearLI);
epicsExportAddress(dset, devPSCUDPShortGetAAI);
epicsExportAddress(dset, devPSCUDPShortGetI24AAI);
}
