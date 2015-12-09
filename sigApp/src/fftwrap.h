/*************************************************************************\
* Copyright (c) 2015 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/
#ifndef FFTWRAP_H
#define FFTWRAP_H

#include <stdlib.h>
#include <complex.h> // Use C99 complex type w/ fftw
#include <time.h>

#include <vector>

#include <fftw3.h>

#include <errlog.h>
#include <dbScan.h>

/* performance timer */
struct PTimer {
    timespec tstart;
    PTimer() {start();}
    void start()
    {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tstart);
    }
    double snap()
    {
        timespec now;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
        double ret = now.tv_sec-tstart.tv_sec + 1e-9*(now.tv_nsec-tstart.tv_nsec);
        tstart = now;
        return ret;
    }
    // Print msg if elapsed time is greater than the given threshold
    void maybeSnap(const char *msg, double threshold=0.0)
    {
        double interval = snap();
        if(interval>threshold) {
            errlogPrintf("%s over threshold %f > %f\n", msg, interval, threshold);
        }
    }
};

// STL compatible allocator which uses fftw_alloc_*() to ensure aligned arrays
template<typename T>
class FFTWAllocator : public std::allocator<T>
{
public:
#define PTYPE(P) typedef typename std::allocator<T>::P P
    PTYPE(pointer);
    PTYPE(const_pointer);
    PTYPE(reference);
    PTYPE(const_reference);
    PTYPE(size_type);
#undef PTYPE

    inline FFTWAllocator() {}

    template<typename U> struct rebind {typedef FFTWAllocator<U> other;};

    inline pointer address(reference x) const { return &x; }
    inline const_pointer address(const_reference x) const { return &x; }
    inline size_type max_size()const {return ((size_t)-1)/sizeof(T);}

    inline void construct(pointer p, const_reference &val)
    {::new((void*)p) T(val);}

    inline void destroy(pointer p)
    {p->~T();}

    inline pointer allocate(size_type n, const void * =0)
    {
        return (T*)fftw_malloc(n*sizeof(T));
    }

    inline void deallocate(pointer p, size_type n)
    {
        fftw_free(p);
    }
};

// Helper to ensure that plans are destroyed
class Plan
{
    fftw_plan plan;
public:
    Plan() :plan(NULL) {}
    ~Plan() {clear();}
    void clear()
    {
        if(plan)
            fftw_destroy_plan(plan);
        plan = NULL;
    }
    void reset(fftw_plan p=NULL)
    {
        if(!p)
            throw std::bad_alloc();
        if(plan)
            fftw_destroy_plan(p);
        plan = p;
    }
    Plan& operator=(fftw_plan p) {reset(p); return *this;}
    fftw_plan get() const {return plan;}
};

struct PSDCalc
{
    enum Window {None=0, Hann} windowtype;
    std::vector<double> window, outint;

    std::vector<double, FFTWAllocator<double> > input, output;

    std::vector<Plan> plans;
    typedef std::vector<fftw_complex, FFTWAllocator<fftw_complex> > middle_inner;
    std::vector<middle_inner> middle;

    std::vector<double> fscale;
    double totalpowertime, totalpowerfreq;

    size_t nfft;
    double fsamp;
    double mult;
    bool replan, newval;

    PSDCalc();
    ~PSDCalc();

    bool set_nfft(size_t n);
    bool set_fsamp(double f);

    bool set_input(double* p, size_t n, double mult=1.0);
    void calculate();
};

#endif // FFTWRAP_H
