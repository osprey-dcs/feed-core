
#include <iostream>

#include <drvSup.h>
#include <iocsh.h>

#include "device.h"

#include <epicsExport.h>

static long feed_report(int lvl)
{
    for(Device::devices_t::const_iterator it(Device::devices.begin()), end(Device::devices.end());
        it != end; ++it)
    {
        it->second->show(std::cout, lvl);
    }
    return 0;
}

static drvet drvFEED = {
    2,
    (DRVSUPFUN)feed_report,
    NULL,
};

epicsExportAddress(drvet, drvFEED);
