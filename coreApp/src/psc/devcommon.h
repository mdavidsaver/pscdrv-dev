/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef DEVCOMMON_H
#define DEVCOMMON_H

#include <cstdio>
#include <cstring>
#include <sstream>
#include <memory>

#include <epicsAssert.h>
#include <devLib.h> // for S_dev_* codes
#include <devSup.h>
#include <recGbl.h>
#include <dbScan.h>
#include <menuConvert.h>
#include <alarm.h>
#include <link.h>
#include <dbStaticLib.h>
#include <dbAccess.h>
#include <dbCommon.h>

#include "device.h"
#include "util.h"

#if EPICS_VERSION_INT>=VERSION_INT(7,0,0,0)
#  define PSCDRV_USE64
#endif

#define CATCH(NAME, REC) \
    catch(recAlarm& e) {\
     (void)recGblSetSevr(REC, e.status, e.severity);\
     return S_dev_badRequest;\
    }catch(std::exception& e) {\
     (void)recGblSetSevr(REC, COMM_ALARM, INVALID_ALARM);\
     timefprintf(stderr, "%s: " #NAME " error: %s\n", (REC)->name, e.what());\
     return S_dev_badRequest;\
    }

template<typename R>
struct dsetTyped {
    long	number;
    long (*report)(int);
    long (*init)(int);
    long (*init_record)(R*);
    long (*get_iointr_info)(int cmd, dbCommon *prec, IOSCANPVT *io);
};

template<typename R>
struct dset6 {
    dsetTyped<R> com;
    long (*readwrite)(R*);
    long (*extra)(R*);
};

#define MAKEDSET(REC, NAME, INIT_REC, IOINTR, READ) \
static dset6<REC ## Record> NAME = {{6, NULL, NULL, (INIT_REC), (IOINTR)}, (READ), NULL}

struct Priv
{
    dbCommon *prec;
    PSCBase *psc;
    epicsUInt16 bid;
    Block *block;
    unsigned long offset;
    long step;

    bool timeFromBlock;
    unsigned long tsoffset;

    template<class R>
    Priv(R*pr) : prec((dbCommon*)pr), psc(0), bid(0), block(0), offset(0), step(0), timeFromBlock(false) {}
};

void parse_link(Priv* priv, const char* link, int direction);

void setRecTimestamp(Priv *priv);

template<typename F, typename I, typename R>
F analogRaw2EGU(R* prec, I rval)
{
    F v = (F)rval + (F)prec->roff;

    if(prec->aslo!=0.0)
        v *= prec->aslo;
    v += prec->aoff;

    switch(prec->linr) {
    case menuConvertLINEAR:
    case menuConvertSLOPE:
        v += prec->eoff;
        v *= prec->eslo;
    }

    return v;
}

template<typename I, typename F, typename R>
I analogEGU2Raw(R* prec, F v)
{
    switch(prec->linr) {
    case menuConvertLINEAR:
    case menuConvertSLOPE:
        v -= prec->eoff;
        v /= prec->eslo;
    }

    v -= prec->aoff;
    if(prec->aslo!=0.0)
        v /= prec->aslo;

    v -= prec->roff;

    return (I)v;
}

class RecInfo
{
    DBENTRY ent;
public:
    RecInfo(const char* recname);
    RecInfo(dbCommon *prec);
    ~RecInfo();
    const char* get(const char* iname);
};

namespace detail {
    template<int L> struct nswap {};
    template<> struct nswap<1> {
        typedef epicsInt8 type;
        static type op(type in) {return in;}
    };
    template<> struct nswap<2> {
        typedef epicsInt16 type;
        static type op(type in) {return htons(in);}
    };
    template<> struct nswap<4> {
        typedef epicsInt32 type;
        static type op(type in) {return htonl(in);}
    };
    template<> struct nswap<8> {
        typedef int64_t type;
        static type op(type in) {
            epicsUInt32 h = in>>32,
                        l = in;
            type out = htonl(l);
            out<<=32;
            return out|htonl(h);
        }
    };

    template<typename T>
    union punny {
        typename nswap<sizeof(T)>::type P;
        T O;
    };
}

template<typename T>
T hton(T in)
{
    detail::punny<T> X;
    X.O = in;
    X.P = detail::nswap<sizeof(T)>::op(X.P);
    return X.O;
}
template<typename T>
T ntoh(T in)
{
    // hton and ntoh are symatrical for big and little endian.
    return hton(in);
}

namespace detail {
    template<typename T>
    union b2type {
        char bytes[sizeof(T)];
        T val;
    };
}

template<typename T>
T bytes2val(const char* bytes)
{
    detail::b2type<T> pun;
    memcpy(pun.bytes, bytes, sizeof(T));
    return ntoh(pun.val);
}

#endif // DEVCOMMON_H
