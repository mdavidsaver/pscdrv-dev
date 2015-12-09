/*************************************************************************\
* Copyright (c) 2015 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <stdio.h>
#include <string.h>

#include <vector>
#include <map>
#include <stdexcept>
#include <memory>
#include <string>
#include <algorithm>

#include <epicsString.h>
#include <cantProceed.h>
#include <epicsMutex.h>
#include <epicsGuard.h>
#include <epicsThread.h>
#include <dbDefs.h>
#include <dbAccess.h>
#include <initHooks.h>
#include <dbStaticLib.h>
#include <mbboRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <aiRecord.h>
#include <aoRecord.h>
#include <waveformRecord.h>
#include <dbCommon.h>
#include <menuFtype.h>

#include <dbScan.h>

#include <fftw3.h>

#include "psc/devcommon.h"
#include "fftwrap.h"

namespace {

typedef epicsGuard<epicsMutex> Guard;

struct Calc : public epicsThreadRunable
{
    std::string name;
    epicsMutex lock;
    epicsEvent wake;
    epicsThread worker;

    PSDCalc::Window windowtype;
    double fsamp;
    double mult;
    size_t nfft;
    epicsUInt32 sampStart, sampWidth;

    epicsTimeStamp timein, timeout;
    std::vector<double, FFTWAllocator<double> > valin, valout;
    std::vector<double> window, scaleout, valoutint;
    double totalpowertime, totalpowerfreq;
    bool valid;

    double lasttime;
    PTimer calctime;

    IOSCANPVT valueScan, scaleScan;

    PSDCalc calc;

    Calc(const std::string& name)
        :name(name)
        ,worker(*this, "PSDCalc",
                epicsThreadGetStackSize(epicsThreadStackBig), //FFTW planner needs a large stack!
                epicsThreadPriorityMedium)
        ,fsamp(0.0)
        ,mult(1.0)
        ,nfft(0)
        ,sampStart(0)
        ,sampWidth(0)
        ,totalpowertime(0.0)
        ,totalpowerfreq(0.0)
        ,valid(false)
    {
        timein.secPastEpoch = 0;
        timein.nsec = 0;
        timeout.secPastEpoch = 0;
        timeout.nsec = 0;
        scanIoInit(&valueScan);
        scanIoInit(&scaleScan);
    }

    void run();

    void poke()
    {
        wake.signal();
    }
};

/* no locking as this list only written during (single-threaded) initialization */
typedef std::map<std::string, Calc*> calcs_t;
static calcs_t calcs;

long commonInit(dbCommon *prec, const char *linkstr, int ret)
{
    std::string lstr(linkstr);

    Calc *pcalc;

    calcs_t::const_iterator it = calcs.find(lstr);
    if(it!=calcs.end())
        pcalc = it->second;
    else {
        pcalc = new Calc(lstr);
        calcs[lstr] = pcalc;
    }
    if(prec->tpro>1)
        fprintf(stderr, "%s: Bind %p '%s'\n", prec->name, pcalc, linkstr);

    prec->dpvt = pcalc;

    return ret;
}

template<class R>
long init_record_inp(R* prec)
{try{ return commonInit((dbCommon*)prec, prec->inp.value.instio.string, 0);}CATCH(__FUNCTION__, prec) }

template<class R>
long init_record_out(R* prec)
{try{ return commonInit((dbCommon*)prec, prec->out.value.instio.string, 0);}CATCH(__FUNCTION__, prec) }

template<class R>
long init_record_out2(R* prec)
{try{ return commonInit((dbCommon*)prec, prec->out.value.instio.string, 2);}CATCH(__FUNCTION__, prec) }

void startWorkers(initHookState state)
{
    if(state!=initHookAfterIocRunning)
        return;
    for(calcs_t::const_iterator it=calcs.begin(), end=calcs.end();
        it!=end; ++it)
    {
        it->second->worker.start();
    }
}

long set_nfft(longoutRecord* prec)
{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
try{
    Guard(priv->lock);
    if(priv->nfft==(unsigned)prec->val)
        return 0;
    priv->nfft = prec->val;
    if(prec->tpro>1)
        fprintf(stderr, "%s: set nfft %u\n", prec->name, (unsigned)priv->nfft);
    priv->poke();
    return 0;
}CATCH(__FUNCTION__, prec)
}

static
long set_fsamp(aoRecord* prec)
{
try{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
    double fsamp = analogRaw2EGU<double>(prec, prec->val);
    Guard(priv->lock);
    if(priv->fsamp==fsamp)
        return 0;
    priv->fsamp = fsamp;
    if(prec->tpro>1)
        fprintf(stderr, "%s: set fsamp %f\n", prec->name, priv->fsamp);
    priv->poke();
    return 2;
}CATCH(__FUNCTION__, prec)
}

long set_scale(aoRecord *prec)
{
try{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
    double mult = analogRaw2EGU<double>(prec, prec->val);
    Guard(priv->lock);
    if(priv->mult==mult)
        return 0;
    priv->mult = mult;
    if(prec->tpro>1)
        fprintf(stderr, "%s: set scale %f\n", prec->name, priv->mult);
    priv->poke();
    return 2;
}CATCH(__FUNCTION__, prec)
}

long set_start(longoutRecord *prec)
{
try{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
    Guard(priv->lock);
    if(priv->sampStart==(epicsUInt32)prec->val)
        return 0;
    priv->sampStart = prec->val;
    if(prec->tpro>1)
        fprintf(stderr, "%s: set start %d\n", prec->name, (int)priv->sampStart);
    return 2;
}CATCH(__FUNCTION__, prec)
}

long set_width(longoutRecord *prec)
{
try{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
    Guard(priv->lock);
    if(priv->sampWidth==(epicsUInt32)prec->val)
        return 0;
    priv->sampWidth = prec->val;
    if(prec->tpro>1)
        fprintf(stderr, "%s: set width %d\n", prec->name, (int)priv->sampWidth);
    return 2;
}CATCH(__FUNCTION__, prec)
}

long set_windtype(mbboRecord *prec)
{
try{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
    Guard(priv->lock);
    switch(prec->rval) {
    case PSDCalc::None:
    case PSDCalc::Hann:
        priv->windowtype=(PSDCalc::Window)prec->rval;
        if(prec->tpro>1)
            fprintf(stderr, "%s: set windowtype %d\n", prec->name, priv->windowtype);
        break;
    default:
        (void)recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
    }
    return 0;
}CATCH(__FUNCTION__, prec)
}

long get_totalptime(aiRecord *prec)
{
try{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
    Guard(priv->lock);
    double val = analogEGU2Raw<double>(prec, priv->totalpowertime);
    prec->val = val;
    prec->udf = 0;

    if(prec->tse==-2) {
        prec->time = priv->timeout;
    }
    return 2;
}CATCH(__FUNCTION__, prec)
}

long get_totalpfreq(aiRecord *prec)
{
try{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
    Guard(priv->lock);
    double val = analogEGU2Raw<double>(prec, priv->totalpowerfreq);
    prec->val = val;
    prec->udf = 0;

    if(prec->tse==-2) {
        prec->time = priv->timeout;
    }
    return 2;
}CATCH(__FUNCTION__, prec)
}

long get_lasttime(aiRecord *prec)
{
try{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
    Guard(priv->lock);
    double val = analogEGU2Raw<double>(prec, priv->lasttime);
    prec->val = val;
    prec->udf = 0;

    if(prec->tse==-2) {
        prec->time = priv->timeout;
    }
    return 2;
}CATCH(__FUNCTION__, prec)
}

static
long init_wf_in(waveformRecord *prec)
{
try{
    switch(prec->inp.type) {
    case CONSTANT: prec->nord = 0; break;
    case PV_LINK:
    case DB_LINK:
    case CA_LINK: break;
    default:
        return S_db_badField;
    }
    if(prec->ftvl!=menuFtypeDOUBLE)
        throw std::runtime_error("Unsupported FTVL");

    RecInfo info((dbCommon*)prec);
    const char * cname = info.get("CALCNAME");
    if(cname)
        return commonInit((dbCommon*)prec, cname, 0);
    else
        throw std::runtime_error("Missing info(CALCNAME");
}CATCH(__FUNCTION__, prec)
}

long set_wf_input(waveformRecord *prec)
{
    double *val = (double*)prec->bptr;
    Calc *priv = (Calc*)prec->dpvt;
try{
    if(!priv) {
        // Link retargeting wipes DPVT
        // find it again.
        RecInfo info((dbCommon*)prec);
        const char * cname = info.get("CALCNAME");
        if(!cname)
            return 0;
        calcs_t::const_iterator it = calcs.find(cname);
        if(it==calcs.end())
            return 0;
        priv = it->second;
        prec->dpvt = priv;
    }

    bool usewindow = priv->sampStart>=0 && priv->sampWidth>0;

    long nReq = prec->nelm;

    if(usewindow && nReq>priv->sampStart+priv->sampWidth)
        nReq = priv->sampStart+priv->sampWidth;

    PTimer runtime;
    epicsTimeStamp srctime;
    if(!dbGetLink(&prec->inp, prec->ftvl, prec->bptr, NULL, &nReq)) {
        prec->nord = nReq;
        if(dbGetTimeStamp(&prec->inp, &srctime))
        {
            // no link time (CONSTANT)
            epicsTimeGetCurrent(&srctime);
        }
    } else
        epicsTimeGetCurrent(&srctime);

    if(usewindow && prec->nord>priv->sampWidth)
        prec->nord = priv->sampWidth;

    {
        Guard(priv->lock);
        priv->calctime.start();

        if(usewindow)
            val += priv->sampStart;

        priv->timein = srctime;
        priv->valin.resize(prec->nord);
        memcpy(&priv->valin[0], val, prec->nord*sizeof(double));

        if(prec->tpro>1)
            fprintf(stderr, "%s: set input %u\n", prec->name, (unsigned)prec->nord);

        priv->poke();
    }
    runtime.maybeSnap(__FUNCTION__, 5e-3);

    if(prec->tse==-2) {
        prec->time = srctime;
    }

    return 0;
}CATCH(__FUNCTION__, prec)
}

#define CHKVALID(self) if(!(self)->valid) {(void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM); return 0;}

long get_wf_output(waveformRecord *prec)
{
    double *val = (double*)prec->bptr;
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
try{
    Guard(priv->lock);
    CHKVALID(priv)

    epicsUInt32 N = std::min(prec->nelm,
                             (epicsUInt32)priv->valout.size());

    memcpy(val, &priv->valout[0], N*sizeof(double));
    prec->nord = N;
    if(prec->nord==0) {
        prec->nord = 1;
        val[0] = 0.0;
    }

    if(prec->tse==-2) {
        prec->time = priv->timeout;
    }
    priv->lasttime = priv->calctime.snap();

    return 0;
}CATCH(__FUNCTION__, prec)
}

long get_wf_outputint(waveformRecord *prec)
{
    double *val = (double*)prec->bptr;
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
try{
    Guard(priv->lock);
    CHKVALID(priv)

    epicsUInt32 N = std::min(prec->nelm,
                             (epicsUInt32)priv->valoutint.size());

    memcpy(val, &priv->valoutint[0], N*sizeof(double));
    prec->nord = N;
    if(prec->nord==0) {
        prec->nord = 1;
        val[0] = 0.0;
    }

    if(prec->tse==-2) {
        prec->time = priv->timeout;
    }
    priv->lasttime = priv->calctime.snap();

    return 0;
}CATCH(__FUNCTION__, prec)
}

long get_wf_fscale(waveformRecord *prec)
{
    double *val = (double*)prec->bptr;
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
try{
    Guard(priv->lock);
    CHKVALID(priv)

    epicsUInt32 N = std::min(prec->nelm,
                             (epicsUInt32)priv->scaleout.size());

    memcpy(val, &priv->scaleout[0], N*sizeof(double));
    prec->nord = N;
    if(prec->nord==0) {
        prec->nord = 1;
        val[0] = 0.0;
    }

    return 0;
}CATCH(__FUNCTION__, prec)
}

long get_wf_window(waveformRecord *prec)
{
    double *val = (double*)prec->bptr;
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
try{
    Guard(priv->lock);
    CHKVALID(priv)

    epicsUInt32 N = std::min(prec->nelm,
                             (epicsUInt32)priv->window.size());

    memcpy(val, &priv->window[0], N*sizeof(double));
    prec->nord = N;
    if(prec->nord==0) {
        prec->nord = 1;
        val[0] = 0.0;
    }

    return 0;
}CATCH(__FUNCTION__, prec)
}

long get_iointr_value(int cmd, dbCommon *prec, IOSCANPVT *io)
{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
try{
    Guard(priv->lock);
    *io = priv->valueScan;

    return 0;
}CATCH(__FUNCTION__, prec)
}

long get_iointr_fscale(int cmd, dbCommon *prec, IOSCANPVT *io)
{
    Calc *priv = (Calc*)prec->dpvt;
    if(!priv)
        return 0;
try{
    Guard(priv->lock);
    *io = priv->scaleScan;

    return 0;
}CATCH(__FUNCTION__, prec)
}

void Calc::run()
{
    while(1) {
        wake.wait();
        PTimer runtime;
        epicsTimeStamp timeproc;
        bool scalchange = false;
        {
            Guard g(lock);

            if(fsamp<=0.0 || nfft==0) {
                valid = false;
                scanIoRequest(valueScan);
                //fprintf(stderr, "Incomplete %f %u\n", fsamp, (unsigned)nfft);
                continue;
            }
            //fprintf(stderr, "Calculate\n");

            if(calc.windowtype != windowtype) {
                calc.windowtype = windowtype;
                calc.replan = true; // not strictly necessary
                scalchange = true;
            }
            scalchange |= calc.set_fsamp(fsamp);
            scalchange |= calc.set_nfft(nfft);
            scalchange |= calc.set_input(&valin[0], valin.size(), mult); // array copy
            timeproc = timein;
            runtime.maybeSnap("run() Prepare", 1e-3);
        }

        calc.calculate();
        runtime.maybeSnap("run() Calculate", 0.05);

        {
            Guard g(lock);
            valid = true;

            totalpowertime = calc.totalpowertime;
            totalpowerfreq = calc.totalpowerfreq;

            valout = calc.output; // array copy
            if(valout.size()==0) {
                valout.push_back(0);
                valid = false;
            }

            valoutint = calc.outint; // array copy
            if(valoutint.size()==0) {
                valoutint.push_back(0);
                valid = false;
            }

            if(scalchange) {
                scaleout = calc.fscale; // array copy
                if(scaleout.size()==0) {
                    scaleout.push_back(0);
                    valid = false;
                }
                window = calc.window;
            }
            timeout = timeproc;
        }
        runtime.maybeSnap("run() Results", 1e-3);

        if(scalchange)
            scanIoRequest(scaleScan);

        scanIoRequest(valueScan);
    }
}

MAKEDSET(mbbo, devMBBOFFTsetwin, init_record_out<mbboRecord>, NULL, set_windtype);
MAKEDSET(longout, devLOFFTnfft, init_record_out<longoutRecord>, NULL, set_nfft);
MAKEDSET(longout, devLOFFTstart, init_record_out<longoutRecord>, NULL, set_start);
MAKEDSET(longout, devLOFFTwidth, init_record_out<longoutRecord>, NULL, set_width);
MAKEDSET(ao, devAOFFTFSamp, init_record_out2<aoRecord>, NULL, set_fsamp);
MAKEDSET(ao, devAOFFTScale, init_record_out2<aoRecord>, NULL, set_scale);
MAKEDSET(ai, devAIFFTTotPwrTime, init_record_inp<aiRecord>, get_iointr_value, get_totalptime);
MAKEDSET(ai, devAIFFTTotPwrFreq, init_record_inp<aiRecord>, get_iointr_value, get_totalpfreq);
MAKEDSET(ai, devAIFFTLasttime, init_record_inp<aiRecord>, get_iointr_value, get_lasttime);
MAKEDSET(waveform, devWFFFTInput, init_wf_in, NULL, set_wf_input);
MAKEDSET(waveform, devWFFFTOutput, init_record_inp<waveformRecord>, get_iointr_value, get_wf_output);
MAKEDSET(waveform, devWFFFTOutputInt, init_record_inp<waveformRecord>, get_iointr_value, get_wf_outputint);
MAKEDSET(waveform, devWFFFTFScale, init_record_inp<waveformRecord>, get_iointr_fscale, get_wf_fscale);
MAKEDSET(waveform, devWFFFTFWindow, init_record_inp<waveformRecord>, get_iointr_fscale, get_wf_window);

}// namespace

#include <epicsExport.h>

epicsExportAddress(dset, devMBBOFFTsetwin);
epicsExportAddress(dset, devLOFFTnfft);
epicsExportAddress(dset, devLOFFTstart);
epicsExportAddress(dset, devLOFFTwidth);
epicsExportAddress(dset, devAOFFTFSamp);
epicsExportAddress(dset, devAOFFTScale);
epicsExportAddress(dset, devAIFFTTotPwrTime);
epicsExportAddress(dset, devAIFFTTotPwrFreq);
epicsExportAddress(dset, devAIFFTLasttime);
epicsExportAddress(dset, devWFFFTInput);
epicsExportAddress(dset, devWFFFTOutput);
epicsExportAddress(dset, devWFFFTOutputInt);
epicsExportAddress(dset, devWFFFTFScale);
epicsExportAddress(dset, devWFFFTFWindow);

static void fftcalcReg()
{
    initHookRegister(startWorkers);
}

epicsExportRegistrar(fftcalcReg);
