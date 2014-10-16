/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include "psc/util.h"

#include <stdarg.h>

#include <epicsTime.h>

// eg "2014-01-01 12:14:15"
#define FMT "%Y-%m-%d %H:%M:%S"

static
int timevfprintf(FILE* fp, const char *str, va_list args)
{
    epicsTimeStamp now;
    int ret, N;
    char tsbuf[25];

    epicsTimeGetCurrent(&now);
    epicsTimeToStrftime(tsbuf, sizeof(tsbuf), FMT, &now);

    ret = fprintf(fp, "%s: ", tsbuf);

    N = vfprintf(fp, str, args);
    if(N>=0 && ret>=0)
      ret += N;
    else if(N<0)
      ret = N;

    return ret;
}

int timefprintf(FILE* fp, const char *str, ...)
{
    int ret;
    va_list args;
    va_start(args, str);
    ret = timevfprintf(fp, str, args);
    va_end(args);
    return ret;
}

int timeprintf(const char *str, ...)
{
    int ret;
    va_list args;
    va_start(args, str);
    ret = timevfprintf(stdout, str, args);
    va_end(args);
    return ret;
}
