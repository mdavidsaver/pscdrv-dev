/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <math.h>

#include <epicsEndian.h>
#include <menuConvert.h>
#include <epicsUnitTest.h>
#include <testMain.h>

#include "psc/devcommon.h"

#define testDblEq(A,B) testOk(fabs((A)-(B))<1e-6, #A " (%f) == " #B " (%f)", A, B)

#if EPICS_BYTE_ORDER==EPICS_ENDIAN_BIG
#define TEST16 0x1234
#define TEST32 0x12345678
#define TEST64 0x123456789abcdeffull
#else /* EPICS_BYTE_ORDER==EPICS_ENDIAN_BIG */
#define TEST16 0x3412
#define TEST32 0x78563412
#define TEST64 0xffdebc9a78563412ull
#endif /* EPICS_BYTE_ORDER==EPICS_ENDIAN_BIG */

static void test_bswap(void)
{
    epicsUInt8 u8 = 0x42;
    epicsUInt16 u16 = TEST16;
    epicsUInt32 u32 = TEST32;
    uint64_t u64 = TEST64;

    testDiag("test byte swapping");

    testOk1(hton(u8)==0x42);
    testOk1(hton(u16)==0x1234);
    testOk1(hton(u32)==0x12345678);
    testOk1(hton(u64)==0x123456789abcdeffull);

    testOk1(hton(hton(4.0e12))==4.0e12);
}

struct analogRecord {
    epicsEnum16 linr;
    double aslo, aoff, eslo, eoff;
    epicsInt32 roff;
};

static void test_EGU2Raw(void)
{
    analogRecord rec;

    testDiag("test analog scaling");

    memset(&rec, 0, sizeof(rec));
    rec.linr = menuConvertNO_CONVERSION;

    testDblEq(analogEGU2Raw<double>(&rec, 4.0),4.0);
    testDblEq(analogEGU2Raw<double>(&rec, 4.2),4.2);
    testDblEq(analogEGU2Raw<double>(&rec, 4.5),4.5);
    testDblEq(analogEGU2Raw<double>(&rec, 4.6),4.6);
    testDblEq(analogEGU2Raw<double>(&rec, 5.0),5.0);

    testDblEq(analogEGU2Raw<double>(&rec, -4.0),-4.0);
    testDblEq(analogEGU2Raw<double>(&rec, -4.2),-4.2);
    testDblEq(analogEGU2Raw<double>(&rec, -4.5),-4.5);
    testDblEq(analogEGU2Raw<double>(&rec, -4.6),-4.6);
    testDblEq(analogEGU2Raw<double>(&rec, -5.0),-5.0);

    rec.aslo = 0.1;
    rec.aoff = -1.0;

    testDblEq(analogEGU2Raw<double>(&rec, 4.0),50.0);
    testDblEq(analogEGU2Raw<double>(&rec, -4.0),-30.0);

    rec.eslo = 0.5;
    rec.eoff = -1.0;

    testDblEq(analogEGU2Raw<double>(&rec, 4.0),50.0);
    testDblEq(analogEGU2Raw<double>(&rec, -4.0),-30.0);

    rec.linr = menuConvertLINEAR;

    testDblEq(analogEGU2Raw<double>(&rec, 4.0),110.0);
    testDblEq(analogEGU2Raw<double>(&rec, -4.0),-50.0);
}

static void test_Raw2EGU(void)
{
    analogRecord rec;

    testDiag("test raw to analog");

    memset(&rec, 0, sizeof(rec));
    rec.linr = menuConvertNO_CONVERSION;

    testDblEq(analogRaw2EGU<double>(&rec, 1.1),1.1);
    testDblEq(analogRaw2EGU<double>(&rec, -1.1),-1.1);

    rec.aslo = 0.1;
    rec.aoff = -1.0;

    testDblEq(analogRaw2EGU<double>(&rec, 1.1),-0.89);
    testDblEq(analogRaw2EGU<double>(&rec, -1.1),-1.11);

    rec.eslo = 10.0;
    rec.eoff = 1.0;

    testDblEq(analogRaw2EGU<double>(&rec, 1.1),-0.89);
    testDblEq(analogRaw2EGU<double>(&rec, -1.1),-1.11);

    rec.linr = menuConvertLINEAR;

    testDblEq(analogRaw2EGU<double>(&rec, 1.1),1.1);
    testDblEq(analogRaw2EGU<double>(&rec, -1.1),-1.1);

}

MAIN(testValues) {
    testPlan(0);
    test_bswap();
    test_EGU2Raw();
    test_Raw2EGU();
    return testDone();
}
