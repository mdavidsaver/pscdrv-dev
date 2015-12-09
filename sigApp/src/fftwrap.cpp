/*************************************************************************\
* Copyright (c) 2015 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <string.h>
#include <math.h>

#include <string>
#include <memory>

#include <epicsMutex.h>
#include <epicsGuard.h>

#include "fftwrap.h"

PSDCalc::PSDCalc()
    :totalpowertime(0.0)
    ,totalpowerfreq(0.0)
    ,nfft(0)
    ,fsamp(0.0)
    ,mult(0.0)
    ,replan(true)
    ,newval(true)
{}

PSDCalc::~PSDCalc()
{}

bool PSDCalc::set_nfft(size_t n)
{
    bool changed = n!=nfft;
    if(changed) {
        nfft=n;
        replan=true;
    }
    return changed;
}

bool PSDCalc::set_fsamp(double f)
{
    bool changed = fsamp!=f;
    if(changed) {
        fsamp=f;
        replan=true; // strictly speaking not needed
    }
    return changed;
}

bool PSDCalc::set_input(double *p, size_t n, double mult)
{
    if(mult==0)
        mult = 1.0;
    bool changed = n!=input.size() || mult!=this->mult;
    if(changed) {
        input.resize(n);
        this->mult = mult;
        replan=true;
    }
    std::copy(p, p+n, input.begin());
    newval = true;
    return changed;
}

// global lock around FFTW planner
epicsMutex fftwplanlock;

static double PI = 3.141592653589793;

void PSDCalc::calculate()
{
    if(input.size()==0 || nfft==0)
        return;
    PTimer runtime;

    // number of time samples per calculation
    size_t ntime = std::max((size_t)0, std::min(nfft, input.size()));
    // number of frequency samples per calculation
    size_t nfreq = ntime/2+1;
    // number of calculations
    size_t nbins = input.size()/ntime;

    assert(ntime>0);
    assert(nfreq>0);
    assert(nbins>0);
    assert(nbins*ntime <= input.size());

    if(replan) {
        window.resize(input.size());
        switch(windowtype){
        case Hann:{
            // Hann window
            double fact = PI/(ntime-1);
            for(size_t n=0, N=window.size(); n<N; n++)
            {
                double temp = sin(fact*n);
                window[n] = temp*temp;
            }
            break;
        }
        case None:
        default:
            std::fill(window.begin(), window.end(), 1.0);
        }

    }

    if(newval) {
        // optimization.  Don't use operator[] in a tight loop, it doesn't always get inline'd
        double *inp = &input[0];
        double *win = &window[0];

        const size_t N = input.size();

        double mean = 0.0;
        for(size_t i=0; i<N; i++)
            mean += inp[i];
        mean /= N;

        double totp = 0.0;
        for(size_t i=0; i<N; i++) {
            double temp = (inp[i]-mean)/mult;
            inp[i] = temp*win[i];
            totp += temp*temp;
        }
        totp /= N;
        totalpowertime = totp;
        newval = false;
        runtime.maybeSnap("calculate() prep. input", 5e-3);
    }

    if(replan) {
        errlogPrintf("%p: Replanning\n", this);

        // reallocate

        plans.clear(); // free existing plans
        plans.resize(nbins);
        middle.resize(nbins);
        for(size_t i=0; i<nbins; i++)
            middle[i].resize(nfreq);
        output.resize(nfreq-1); // exclude 0Hz
        outint.resize(nfreq-1);

        // re-do frequency scale
        fscale.resize(nfreq-1);
        double mult = fsamp/ntime;
        for(size_t i=0; i<fscale.size(); i++)
            fscale[i] = (i+1)*mult;

        epicsGuard<epicsMutex> pg(fftwplanlock);

        for(size_t i=0; i<nbins; i++)
        {
            // FFTW_EXHAUSTIVE > FFTW_PATIENT > FFTW_MEASURE > FFTW_ESTIMATE
            plans[i] = fftw_plan_dft_r2c_1d(ntime, &input[i*ntime], middle[i].data(), FFTW_MEASURE);
        }

        replan = false;
        runtime.maybeSnap("calculate() replan", 0.1);
        errlogPrintf("%p: I have a plan ntime=%u nfreq=%u nbins=%u\n", this, (unsigned)ntime, (unsigned)nfreq, (unsigned)nbins);
    }

    assert(plans.size()==nbins);
    assert(middle.size()==nbins);
    assert(output.size()==nfreq-1); // exclude 0Hz

    for(size_t i=0; i<nbins; i++)
        fftw_execute(plans[i].get());
    runtime.maybeSnap("calculate() execute", 3e-3);

    std::fill(output.begin(), output.end(), 0.0); // zero output array

    {
        double *out=&output[0];
        // sum by frequency for each bin
        for(size_t i=0; i<nbins; i++) {
            fftw_complex *mid=&middle[i][0];
            for(size_t j=1; j<nfreq; j++) {
                fftw_complex temp = mid[j];
                // output[j] += temp**2
                out[j-1] += creal(temp)*creal(temp) + cimag(temp)*cimag(temp);
            }
        }

        // scale by 2.0/ntime**2
        // turn the sum in to an average with /nbins

        double factor = 2.0/(ntime*ntime*nbins);

        totalpowerfreq = 0.0;
        for(size_t i=0; i<nfreq-1; i++) {
            double temp;
            temp = out[i] = out[i]*factor;
            totalpowerfreq += temp;
            outint[i] = sqrt(totalpowerfreq/fsamp)*1e3;
        }
    }

    runtime.maybeSnap("calculate() post-proc", 2e-3);
}
