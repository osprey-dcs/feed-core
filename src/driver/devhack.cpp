#include <stdio.h>

#include <longoutRecord.h>
#include <link.h>

#include <epicsExport.h>

#include "dev.h"

namespace {

// workaround for https://bugs.launchpad.net/epics-base/+bug/1745039

void propLink(dbCommon *src, DBLINK *plink)
{
    if(plink->type!=DB_LINK)
        return;
    // assume target will be processed

    // HACK #1 breaking DB_LINK encapsulation here
    DBADDR *addr = (DBADDR*)plink->value.pv_link.pvt;
    dbCommon *dest = addr->precord;

    if(dest->pact) {
        // target is async record which is busy
        // so record can not be processed.
        // unfortunately Base doesn't schedule
        // for reprocessing due to overly
        // conservative loop avoidance.

        // HACK #2
        // we force reprocessing here, so
        // this dset must *never* appear in
        // a processing loop.
        dest->rpro = 1;

        if(src->tpro>1)
            printf("%s -> %s force RPRO\n", src->name, dest->name);
    }
}

long write_hack(longoutRecord *prec)
{
    propLink((dbCommon*)prec, &prec->out);
    propLink((dbCommon*)prec, &prec->flnk);
    dbPutLink(&prec->out,DBR_LONG, &prec->val,1);
    return 0;
}

}

DSET(devLoFEEDHack, longout, 0, 0, &write_hack)
