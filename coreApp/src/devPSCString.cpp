/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include "psc/devcommon.h"

#include <stringinRecord.h>
#include <stringoutRecord.h>

namespace {

long init_si_record(stringinRecord* prec)
{
    assert(prec->inp.type==INST_IO);
    try {
        std::auto_ptr<Priv> priv(new Priv(prec));

        parse_link(priv.get(), prec->inp.value.instio.string, 0);

        prec->dpvt = (void*)priv.release();

    }CATCH(init_si_record, prec)
    return 0;
}

long init_so_record(stringoutRecord* prec)
{
    assert(prec->out.type==INST_IO);
    try {
        std::auto_ptr<Priv> priv(new Priv(prec));

        parse_link(priv.get(), prec->out.value.instio.string, 1);

        prec->dpvt = (void*)priv.release();

    }CATCH(init_si_record, prec)
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

long read_si(stringinRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;

    try {
        Guard(priv->psc->lock);

        if(!priv->psc->isConnected()) {
            recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            return 0;
        }

        size_t len = priv->block->data.size();
        if(len>=MAX_STRING_SIZE)
            len=MAX_STRING_SIZE-1;

        if(priv->offset > len) {
            // always empty string
            prec->val[0]='\0';
        } else {
            len -= priv->offset;
            strncpy(prec->val, &priv->block->data[priv->offset], len);
            prec->val[len]='\0';
        }

        setRecTimestamp(priv);
    }CATCH(read_si, prec)

    return 0;
}

long write_so(stringoutRecord* prec)
{
    if(!prec->dpvt)
        return -1;
    Priv *priv=(Priv*)prec->dpvt;

    try {
        Guard(priv->psc->lock);

        if(!priv->psc->isConnected()) {
            recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
            return 0;
        }

        size_t len = strnlen(prec->val, MAX_STRING_SIZE);

        priv->psc->queueSend(priv->block, (void*)&prec->val[0], len);

        priv->block->data.resize(len);

        memcpy(&priv->block->data[0], prec->val, len);
    }CATCH(write_so, prec)

    return 0;
}

MAKEDSET(stringin, devPSCBlockSi, &init_si_record, &get_iointr_info, &read_si);
MAKEDSET(stringout, devPSCBlockSo, &init_so_record, &get_iointr_info, &write_so);

} // namespace

#include <epicsExport.h>

epicsExportAddress(dset, devPSCBlockSi);
epicsExportAddress(dset, devPSCBlockSo);
