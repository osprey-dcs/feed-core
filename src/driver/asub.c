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
    field(NEVA, "100")    # max output length
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

/* parse 16-bit mask.
 * For each bit I write output either -1 if not set
 * or N if set.
 * N = the number of set bits <= I
 *
 * eg. A="0x5" (0b101) sets
 *  VALA = 2  # number of bits set
 *  VALB = 0
 *  VALC = -1
 *  VALD = 1
 *  VALE = -1
 *  ...
 *  VALQ = -1
 *
 *
record(asub, "$(N)") {
    field(SNAM, "asub_feed_timebase")
    field(FTA , "LONG")  # mask
    field(FTVA, "LONG")  # number of bits set
    field(FTVB, "LONG")  # -1 or number of bits set at or below
    ...
    field(FTVQ, "LONG")
}
 */
static
long asub_split_bits(aSubRecord *prec)
{
    const size_t nout = aSubRecordVALN - aSubRecordVALB;

    epicsInt32 mask    = *(epicsInt32*)prec->a,
             *bitcnt   = prec->vala,
             **outputs = (epicsInt32**)&prec->valb;

    epicsUInt32 *outcnt = &prec->nevb;
    const epicsUInt32 *outlim = &prec->novb;

    unsigned i;
    const epicsEnum16* outtype = &prec->ftvb;

    *bitcnt = 0;

    for(i=0; mask && i<nout; mask>>=1, i++) {
        if(outtype[i]!=menuFtypeLONG || outlim[i]==0u)
            continue;

        if(mask&1) {
            *outputs[i] = *bitcnt++;

        } else {
            *outputs[i] = -1;
        }

        outcnt[i] = 1u;
    }

    return 0;
}

static
void asubFEEDRegistrar(void)
{
    registryFunctionAdd("asub_feed_timebase", (REGISTRYFUNCTION)&asub_feed_timebase);
    registryFunctionAdd("asub_split_bits", (REGISTRYFUNCTION)&asub_split_bits);
    registryFunctionAdd("sub_count_bits", (REGISTRYFUNCTION)&sub_count_bits);
}
epicsExportRegistrar(asubFEEDRegistrar);
