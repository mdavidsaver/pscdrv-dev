/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef PSC_UTIL_H
#define PSC_UTIL_H

#include <stdio.h>

#include <compilerDependencies.h>

#include <epicsVersion.h>

#if defined(_WIN32) || defined(__CYGWIN__)

#  if defined(BUILDING_pscdrv_API) && defined(EPICS_BUILD_DLL)
/* Building library as dll */
#    define PSC_API __declspec(dllexport)
#  elif !defined(BUILDING_pscdrv_API) && defined(EPICS_CALL_DLL)
/* Calling library in dll form */
#    define PSC_API __declspec(dllimport)
#  endif

#elif __GNUC__ >= 4
#  define PSC_API __attribute__ ((visibility("default")))
#endif

#if !defined(PSC_API)
#  define PSC_API
#endif

#ifndef VERSION_INT
#  define VERSION_INT(V,R,M,P) ( ((V)<<24) | ((R)<<16) | ((M)<<8) | (P))
#endif
#ifndef EPICS_VERSION_INT
#  define EPICS_VERSION_INT VERSION_INT(EPICS_VERSION, EPICS_REVISION, EPICS_MODIFICATION, EPICS_PATCH_LEVEL2)
#endif

#ifdef __cplusplus

#if __cplusplus<201103L
#  define override
#  define final
#  define noexcept throw()
#endif

extern "C" {
#endif

PSC_API
int timefprintf(FILE* fp, const char *str, ...) EPICS_PRINTF_STYLE(2,3);
PSC_API
int timeprintf(const char *str, ...) EPICS_PRINTF_STYLE(1,2);

#ifdef __cplusplus
}
#endif

#endif /* PSC_UTIL_H */
