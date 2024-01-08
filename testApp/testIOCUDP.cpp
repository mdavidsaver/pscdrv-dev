/*************************************************************************\
* Copyright (c) 2024 Michael Davidsaver
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <testMain.h>
#include <epicsUnitTest.h>
#include <dbUnitTest.h>
#include <dbAccess.h>

#include <psc/device.h>

namespace {
struct TestMonitor {
    testMonitor * const mon;
    TestMonitor(const char* pvname, unsigned mask=DBE_VALUE|DBE_ALARM, unsigned opt=0)
        :mon(testMonitorCreate(pvname, mask, opt))
    {
        if(!mon)
            throw std::runtime_error("testMonitorCreate");
    }
    ~TestMonitor() {
        testMonitorDestroy(mon);
    }
    void wait() const {
        testMonitorWait(mon);
    }
};

} // namespace

extern "C"
void testIOC_registerRecordDeviceDriver(struct dbBase*);

MAIN(testIOCUDP) {
    testPlan(5);
    testdbPrepare();

    testdbReadDatabase("testIOC.dbd", NULL, NULL);
    testIOC_registerRecordDeviceDriver(pdbbase);

    PSCDebug = 5;

    {
        PSCUDP R("receiver", "127.0.0.1", 0, 0, 0);
        PSCUDP S("sender", "127.0.0.1", R.bound_port(), 0, 0);

        testdbReadDatabase("../../db/psc-ctrl.db", NULL, "P=tx:,NAME=sender");
        testdbReadDatabase("../../db/psc-ctrl.db", NULL, "P=rx:,NAME=receiver");
        testdbReadDatabase("../testudp.db", NULL, NULL);

        eltc(0);
        testIocInitOk();
        eltc(1);

        {
            TestMonitor M("rx:hear");
            testdbPutFieldOk("tx:say", DBR_STRING, "Testing");
            M.wait();
            testdbGetFieldEqual("rx:hear", DBR_STRING, "Testing");
        }

        {
            TestMonitor M("rx:200:b");

            const epicsUInt32 input[] = {0x11121213, 0x21222331, 0x32333441, 0x42430000};
            const epicsUInt32 expect_a[] = {0x11121213, 0x31323334};
            const epicsUInt32 expect_b[] = {0x212223, 0x414243};

            testdbPutArrFieldOk("tx:200", DBF_LONG, NELEMENTS(input), input);

            M.wait();

            testdbGetArrFieldEqual("rx:200:a", DBF_LONG, NELEMENTS(expect_a)+1, NELEMENTS(expect_a), expect_a);
            testdbGetArrFieldEqual("rx:200:b", DBF_LONG, NELEMENTS(expect_b)+1, NELEMENTS(expect_b), expect_b);
        }

        testIocShutdownOk();
    }

    testdbCleanup();
    return testDone();
}
