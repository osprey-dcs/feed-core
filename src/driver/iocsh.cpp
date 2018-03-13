
#include <sstream>

#include <drvSup.h>
#include <iocsh.h>
#include <initHooks.h>

// redirects stdout/err for iocsh capture
#include <epicsStdio.h>

#include "device.h"

#include <epicsExport.h>

static long feed_report(int lvl)
{
    std::ostringstream strm;
    for(Device::devices_t::const_iterator it(Device::devices.begin()), end(Device::devices.end());
        it != end; ++it)
    {
        strm<<"Device: "<<it->first<<"\n";
        it->second->show(strm, lvl);
    }
    printf("%s", strm.str().c_str());
    return 0;
}

static drvet drvFEED = {
    2,
    (DRVSUPFUN)feed_report,
    NULL,
};

void feed_hook(initHookState state)
{
    if(state!=initHookAfterIocRunning)
        return;
    try {
        for(Device::devices_t::const_iterator it(Device::devices.begin()), end(Device::devices.end());
            it!=end; ++it)
        {
            Device *dev = it->second;
            dev->runner.start();
        }
    }catch(std::exception& e){
        fprintf(stderr, "FEED init hook error: %s\n", e.what());
    }
}

static void feedRegistrar()
{
    initHookRegister(&feed_hook);
}

extern "C" {
epicsExportAddress(drvet, drvFEED);
epicsExportRegistrar(feedRegistrar);
epicsExportAddress(int, feedNumInFlight);
epicsExportAddress(double, feedTimeout);
}
