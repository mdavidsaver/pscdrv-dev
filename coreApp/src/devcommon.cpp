/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <stdexcept>

#include <epicsString.h>

#include "psc/devcommon.h"

void parse_link(Priv* priv, const char* link, int direction)
{
    std::istringstream strm(link);

    std::string name;
    unsigned int block;
    unsigned long offset = 0;
    long step = 0;

    strm >> name >> block;
    if(!strm.eof())
        strm >> offset;
    if(!strm.eof())
        strm >> step;
    if(strm.fail()) {
        timefprintf(stderr, "%s: Error Parsing: '%s'\n",
                priv->prec->name, link);
        throw std::runtime_error("Link parsing error");
    } else if(!strm.eof()) {
        timefprintf(stderr, "%s: link parsing found \'%s\' instead of EOS\n",
                priv->prec->name, strm.str().substr(strm.tellg()).c_str());
    }

    priv->psc = PSC::getPSCBase(name);
    if(!priv->psc) {
        timefprintf(stderr, "%s: PSC '%s' not found\n",
                priv->prec->name, name.c_str());
        throw std::runtime_error("PSC name not known");
    }

    priv->bid = block;
    priv->offset = offset;
    priv->step = step;

    bool onconn;
    {
        RecInfo info(priv->prec);

        const char *tsoffset = info.get("TimeFromBlock");
        if(tsoffset) {
            std::istringstream tsstrm(tsoffset);
            tsstrm >> priv->tsoffset;
            if(tsstrm.fail())
                timefprintf(stderr, "%s: Error processing time offset '%s'\n", priv->prec->name, tsoffset);
            else
                priv->timeFromBlock = true;
        }

        const char *scan = info.get("SYNC");
        onconn = scan && epicsStrCaseCmp(scan, "ProcOnConn")==0;
    }

    Guard(priv->psc->lock);

    switch(direction) {
    case 0: priv->block = priv->psc->getRecv(block); break;
    case 1: priv->block = priv->psc->getSend(block); break;
    default: priv->block = NULL; break;
    }

    if(onconn && direction==1) {
        priv->psc->procOnConnect.push_back(priv->prec);
    }

    if(!priv->block && direction<=1) {
        timefprintf(stderr, "%s: can't get block %u from PSC '%s'\n",
                priv->prec->name, block, name.c_str());
        throw std::runtime_error("PSC can't get block #");
    }
}

namespace {

struct rawts {
    epicsUInt32 sec;
    epicsUInt32 nsec;
};

union tsdata {
    rawts ts;
    char bytes[sizeof(rawts)];
};

#ifdef STATIC_ASSERT
STATIC_ASSERT(sizeof(rawts)==8);
#endif

} // namespace

void setRecTimestamp(Priv *priv)
{
    if(priv->prec->tse!=epicsTimeEventDeviceTime)
        return;

    epicsTime result;

    // extract timestamp from block data
    tsdata raw;

    if(priv->timeFromBlock &&
            priv->block &&
            priv->block->data.copyout_shape(raw.bytes, priv->tsoffset, sizeof(raw.bytes), 0u, 1u)==1
        )
    {
        epicsTimeStamp ts;
        ts.secPastEpoch = ntohl(raw.ts.sec) - POSIX_TIME_AT_EPICS_EPOCH;
        ts.nsec = ntohl(raw.ts.nsec);
        result = ts;

    } else {
        // Use receive time
        result = priv->block->rxtime;
    }

    priv->prec->time = result;
}

RecInfo::RecInfo(const char *recname)
{
    dbInitEntry(pdbbase, &ent);
    long status = dbFindRecord(&ent, recname);
    if(status)
        throw std::runtime_error("Record not found");
}

RecInfo::RecInfo(dbCommon *prec)
{
    dbInitEntry(pdbbase, &ent);
    long status = dbFindRecord(&ent, prec->name);
    if(status)
        throw std::logic_error("Record not found");
}

RecInfo::~RecInfo()
{
    dbFinishEntry(&ent);
}

const char* RecInfo::get(const char *iname)
{
    long status = dbFindInfo(&ent, iname);
    if(status)
        return NULL;
    return dbGetInfoString(&ent);
}
