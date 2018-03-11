#include <stdexcept>

#include <errlog.h>
#include <epicsMath.h>
#include <dbUnitTest.h>
#include <testMain.h>
#include <dbAccess.h>

extern "C" {
void testfeed_registerRecordDeviceDriver(struct dbBase *);
}

static
double testApproxEqual(const char *pv, double expected, double max_delta)
{
    DBADDR addr;
    double ret;
    long status;

    if(dbNameToAddr(pv, &addr)) {
        testFail("Missing PV \"%s\"", pv);
        return epicsNAN;
    }

    status = dbGetField(&addr, DBF_DOUBLE, &ret, 0, 0, 0);
    if (status) {
        testFail("dbGetField(\"%s\", %d, ...) -> 0x%lx", pv, DBF_DOUBLE, status);
        return epicsNAN;
    }
    testOk(fabs(expected-ret)<=max_delta, "%s -> %g ~= %g (max_delta %g)",
           pv, ret, expected, max_delta);

    return ret;
}

struct testdat {
    epicsUInt32 wave_samp_per;
    epicsUInt32 wave_shift;
    double yscale;
};

/** cross-check waveform scaling with python code

from leep.raw import yscale
for nn in [1,2,3,7,10,15,25,50,55,128,200,255]:
    sh, fs = yscale(nn)
    print '    {%d, %d, %.6f},'%(nn, sh, fs)

*/
static const testdat yscale_data[] = {
    {1, 0, 130930.013140},
    {2, 0, 523720.052558},
    {3, 1, 294592.529564},
    {7, 2, 400973.165240},
    {10, 3, 204578.145531},
    {15, 3, 460300.827444},
    {25, 4, 319653.352392},
    {50, 5, 319653.352392},
    {55, 5, 386780.556394},
    {128, 6, 523720.052558},
    {200, 7, 319653.352392},
    {255, 7, 519636.480982},
};

MAIN(testsub)
{
    testPlan(61);
    try {
        testdbPrepare();

        testdbReadDatabase("testfeed.dbd", 0, 0);
        testfeed_registerRecordDeviceDriver(pdbbase);

        testdbReadDatabase("testsub.db", ".:..:", "");

        eltc(0);
        testIocInitOk();
        eltc(1);

        testdbPutFieldOk("yscale.B", DBF_LONG, 33);// cic_period

        for(size_t i=0; i<NELEMENTS(yscale_data); i++) {
            testDiag("[%zu] wave_samp_per=%u wave_shift=%u yscale=%f", i,
                     yscale_data[i].wave_samp_per,
                     yscale_data[i].wave_shift,
                     yscale_data[i].yscale);

            testdbPutFieldOk("yscale.A", DBF_LONG, yscale_data[i].wave_samp_per);

            testdbPutFieldOk("yscale.PROC", DBF_LONG, 1);
            testdbGetFieldEqual("yscale.SEVR", DBF_SHORT, 0);

            testApproxEqual("yscale.VALA", yscale_data[i].wave_shift, 0);
            testApproxEqual("yscale.VALB", yscale_data[i].yscale, 0.0001);
        }

        testIocShutdownOk();

        testdbCleanup();
        errlogFlush();
    }catch(std::exception& e){
        errlogFlush();
        testAbort("Unhandled exception: %s", e.what());
    }
    return testDone();
}
