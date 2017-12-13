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

/** expected values calculated with python script

from math import ceil, log

def yscale(wave_samp_per=1):
    try:
        from math import ceil, log

        lo_cheat = (74694*1.646760258)/2**17

        shift_base = 4
        cic_period=33
        cic_n = wave_samp_per * cic_period

        def log2(n):
            try:
                return log(n,2)
            except ValueError as e:
                raise ValueError("log2(%s) %s"%(n,e))

        shift_min = log2(cic_n**2 * lo_cheat)-12

        wave_shift = max(0, ceil(shift_min/2))

        return wave_shift, lo_cheat * (33 * wave_samp_per)**2 * 4**(8 - wave_shift)/512.0/(2**shift_base)
    except Exception as e:
        raise RuntimeError("yscale(%s) %s"%(wave_samp_per, e))

*/
static const testdat yscale_data[] = {
    {1, 0, 8175.68283},
    {2, 0, 32702.73134},
    {3, 1, 18395.28638},
    {7, 2, 25038.02868},
    {10, 3, 12774.50443},
    {15, 3, 28742.63497},
    {25, 4, 19960.16317},
    {50, 5, 19960.16317},
    {55, 5, 24151.79744},
    {128, 6, 32702.73134},
    {200, 7, 19960.16317},
    {255, 7, 32447.74025},
};

MAIN(testdevice)
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
