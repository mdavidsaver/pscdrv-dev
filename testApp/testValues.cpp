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

namespace {

void test_bswap(void)
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

void test_EGU2Raw(void)
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

void test_Raw2EGU(void)
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

void test_dbuffer_contig()
{
    dbuffer B(12u);
    testOk1(B.size()==12u);
    testOk1(B.nstrides()==1u);

    {
        const char inp[12] = {1,2,3,4,5,6,7,8,9,10,11,12};
        testOk1(!!B.copyin(inp, 0, sizeof(inp)));

        char out[12];
        memset(out, 0xfe, sizeof(out));
        testOk1(!!B.copyout(out, 0, sizeof(inp)));

        testOk1(memcmp(inp, out, sizeof(out))==0);
    }

    {
        const char expect[] = {3,4, 7,8, 11,12};
        char out[10];
        memset(out, 0xfe, sizeof(out));
        testOk1(B.copyout_shape(out, 2, 2, 2, 5)==3);

        testOk1(memcmp(expect, out, sizeof(expect))==0);
    }
}

void dummy_cleanup(const void *data, size_t datalen, void *extra)
{
    testDiag("Cleanup %p %zu", data, datalen);
}

void test_dbuffer_discontrig()
{
    const char inpA[11] = {1,2,3,4,5,6,7,8,9,10,11};
    const char inpB[5] = {12,13,14,15,16};

    dbuffer B;
    {
        evbuffer *temp = evbuffer_new();
        if(!temp)
            testFail("evbuffer_new");

        evbuffer_add_reference(temp, inpA, sizeof(inpA), &dummy_cleanup, 0);
        evbuffer_add_reference(temp, inpB, sizeof(inpB), &dummy_cleanup, 0);

        B.consume(temp);
        testOk1(evbuffer_get_length(temp)==0u);
        evbuffer_free(temp);
    }

    testOk1(B.size()==16u);
    testOk1(B.nstrides()==2u);

    {
        const char expect[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        char out[16];
        memset(out, 0xfe, sizeof(out));
        testOk1(!!B.copyout(out, 0, sizeof(out)));

        testOk1(memcmp(expect, out, sizeof(expect))==0);
    }

    {
        const char expect[] = {3,4, 7,8, 11,12, 15,16};
        char out[16];
        memset(out, 0xfe, sizeof(out));
        testOk1(B.copyout_shape(out, 2, 2, 2, sizeof(out)/2u)==4);

        testOk1(memcmp(expect, out, sizeof(expect))==0);
    }
}

} // namespace

MAIN(testValues) {
    testPlan(43);
    test_bswap();
    test_EGU2Raw();
    test_Raw2EGU();
    test_dbuffer_contig();
    test_dbuffer_discontrig();
    return testDone();
}
