
#include <iostream>

#include <drvSup.h>
#include <iocsh.h>
#include <initHooks.h>

#include "device.h"

#include <epicsExport.h>

static long feed_report(int lvl)
{
    for(Device::devices_t::const_iterator it(Device::devices.begin()), end(Device::devices.end());
        it != end; ++it)
    {
        std::cout<<"Device: "<<it->first<<"\n";
        it->second->show(std::cout, lvl);
    }
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
}
