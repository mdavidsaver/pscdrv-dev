/*************************************************************************\
* Copyright (c) 2015 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <math.h>

#include <aSubRecord.h>
#include <recGbl.h>
#include <alarm.h>
#include <registryFunction.h>
#include <menuFtype.h>
#include <errlog.h>
#include <epicsExport.h>

static int foo;

#define MAGIC ((void*)&wf_stats)
#define BADMAGIC ((void*)&foo)

/* Waveform statistics
 *
 * Computes mean and std of the (subset of)
 * waveform Y.  The values of waveform X
 * (aka time) are used to compute the windows
 *
 * record(aSub, "$(N)") {
 *  field(SNAM, "Wf Stats")
 *  field(FTA , "DOUBLE")
 *  field(FTB , "DOUBLE")
 *  field(FTC , "DOUBLE")
 *  field(FTD , "DOUBLE")
 *  field(FTVA ,"DOUBLE")
 *  field(FTVB ,"DOUBLE")
 *  field(FTVC ,"DOUBLE")
 *  field(FTVD ,"DOUBLE")
 *  field(FTVE ,"ULONG")
 *  field(NOA , "128")
 *  field(NOB , "128")
 *  field(INPA, "Waveform Y")
 *  field(INPB, "Waveform X")
 *  field(INPC, "Start X") # window start
 *  field(INPD, "Width X") # window width
 *  field(OUTA, "Mean PP")
 *  field(OUTB, "Std PP")
 *  field(OUTC, "Min PP")
 *  field(OUTD, "Max PP")
 *  field(OUTE, "N PP")
 */

static
long wf_stats(aSubRecord* prec)
{
    size_t i, N=0;
    epicsEnum16 *ft = &prec->fta,
                *ftv= &prec->ftva;
    // actual length of inputs
    epicsUInt32 len = prec->nea;

    double *data = prec->a,
           *time = prec->b,
           sum   = 0.0, sum2 = 0.0,
           vmin, vmax,
           start = *(double*)prec->c,
           width = *(double*)prec->d;

    char usetime = prec->neb>1;
    if(usetime && prec->neb<prec->nea)
        len = prec->neb;

    if(start<=0 && width<=0) {
        /* default to entire range */
        start = 0.0;
        if(usetime)
            width = time[prec->neb-1];
        else
            width = prec->neb;
    }

    if(prec->dpvt==BADMAGIC)
        return 1;
    if(prec->dpvt!=MAGIC) {
        // Only do type checks in not already passed
        for(i=0; i<4; i++) {
            if(ft[i]!=menuFtypeDOUBLE) {
                prec->dpvt=BADMAGIC;
                errlogPrintf("%s: FT%c must be DOUBLE\n",
                             prec->name, 'A'+(char)i);
                return 1;

            }
        }
        for(i=0; i<2; i++) {
            if(ftv[i]!=menuFtypeDOUBLE) {
                prec->dpvt=BADMAGIC;
                errlogPrintf("%s: FTV%c must be DOUBLE\n",
                             prec->name, 'A'+(char)i);
                return 1;

            }
        }
        prec->dpvt = MAGIC;
    }

    if(len==0) {
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        return 0;
    }

    for(i=0; i<len; i++) {
        if(usetime) {
            if(time[i]<start)
                continue;
            if(time[i]>=start+width)
                break;
        } else {
            if(i<start)
                continue;
            if(i>=start+width)
                break;
        }

        if(N==0) {
            vmin = vmax = data[i];
        } else {
            if(data[i]<vmin)
                vmin = data[i];
            if(data[i]>vmax)
                vmax = data[i];
        }
        sum  += data[i];
        sum2 += data[i] * data[i];
        N++;
    }

    if(N==0) {
        (void)recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        return 0;
    }

    sum  /= N; // <x>
    sum2 /= N; // <x^2>

    *(double*)prec->vala = sum;
    prec->neva=1;
    *(double*)prec->valb = sqrt(sum2 - sum*sum);
    prec->nevb=1;
    *(double*)prec->valc = vmin;
    prec->nevc=1;
    *(double*)prec->vald = vmax;
    prec->nevd=1;

    *(epicsUInt32*)prec->vale = N;
    return 0;
}

epicsRegisterFunction(wf_stats);
