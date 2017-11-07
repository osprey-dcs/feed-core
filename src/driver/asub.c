#include <errno.h>

#include <recGbl.h>
#include <alarm.h>
#include <menuFtype.h>

#include <subRecord.h>
#include <aSubRecord.h>

#include <registryFunction.h>
#include <epicsExport.h>

/* Fill in VALA with sequence
 *  [first, first+step, first+step*2, ...]
 *
 * result length is min(limit, .NEVA)
 *
record(asub, "$(N)") {
    field(SNAM, "asub_feed_timebase")
    field(FTA , "DOUBLE") # first value
    field(FTB , "DOUBLE") # step size
    field(FTC , "ULONG")  # max element count limit
    field(FTVA, "DOUBLE") # output array
}
 */
static
long asub_feed_timebase(aSubRecord *prec)
{
    double val = *(double*)prec->a,
           step= *(double*)prec->b,
          *out = (double*)prec->vala;
    epicsUInt32 limit = *(epicsUInt32*)prec->c,
                i;

    if(limit > prec->nova)
        limit = prec->nova;

    if(prec->fta!=menuFtypeDOUBLE
        || prec->ftb!=menuFtypeDOUBLE
        || prec->ftc!=menuFtypeULONG
        || prec->ftva!=menuFtypeDOUBLE) {
        (void)recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        return EINVAL;
    }

    for(i=0; i<limit; i++, val += step) {
        out[i] = val;
    }

    prec->nova = limit;
    return 0;
}

/* Count number of bits set
 *
record(sub, "$(N)") {
    field(SNAM, "sub_count_bits")
    field(A, "") # bit mask
}
 */
static
long sub_count_bits(subRecord *prec)
{
    epicsUInt32 mask = (epicsUInt32)prec->a;
    prec->val = 0;

    for(; mask; mask>>=1) {
        if(mask&1)
            prec->val++;
    }
    return 0;
}

static
void asubFEEDRegistrar(void)
{
    registryFunctionAdd("asub_feed_timebase", (REGISTRYFUNCTION)&asub_feed_timebase);
    registryFunctionAdd("sub_count_bits", (REGISTRYFUNCTION)&sub_count_bits);
}
epicsExportRegistrar(asubFEEDRegistrar);
