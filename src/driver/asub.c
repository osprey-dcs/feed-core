#include <errno.h>

#include <errlog.h>
#include <recGbl.h>
#include <alarm.h>
#include <menuFtype.h>
#include <epicsMath.h>

#include <subRecord.h>
#include <aSubRecord.h>

#include <registryFunction.h>
#include <epicsExport.h>

/* Fill in VALA with sequence
 *  [first, first+step, first+step*2, ...]
 *
 * result length is min(limit, .NOVA)
 *
record(asub, "$(N)") {
    field(SNAM, "asub_feed_timebase")
    field(FTA , "DOUBLE") # first value
    field(FTB , "DOUBLE") # step size
    field(FTC , "ULONG")  # max element count limit
    field(FTVA, "DOUBLE") # output array
    field(NOVA, "100")    # max output length
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
    if(prec->tpro>1)
        errlogPrintf("%s nova=%u\n", prec->name, (unsigned)prec->nova);
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

    prec->neva = limit;
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
 * N = the number of set bits >= I
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

    unsigned i;
    const epicsEnum16* outtype = &prec->ftvb;

    *bitcnt = 0;

    for(i=0; i<nout; mask<<=1, i++) {
        if(outtype[i]!=menuFtypeLONG)
            continue;

        if(mask&(1u<<(nout-1))) {
            *outputs[i] = (*bitcnt)++;

        } else {
            *outputs[i] = -1;
        }

        outcnt[i] = 1u;
    }

    prec->neva = 1;

    return 0;
}

/* calculate shift and Y scaling factor
 * record(asub, "$(N)") {
 *   field(SNAM, "asub_yscale")
 *   field(FTA , "ULONG") # wave_samp_per register
 *   field(FTB , "ULONG") # cic_period
 *   field(FTVA, "ULONG") # wave_shift
 *   field(FTVB, "DOUBLE") # yscale
 * }
 */
static
long asub_yscale(aSubRecord *prec)
{
    const long shift_base = 4;
    const epicsUInt32 wave_samp_per = *(const epicsUInt32*)prec->a,
                      cic_period    = *(const epicsUInt32*)prec->b;
    epicsUInt32 *wave_shift = (epicsUInt32*)prec->vala;
    double *yscale = (double*)prec->valb;

    const double lo_cheat = (74694*1.646760258)/pow(2, 17);
    const long   cic_n    = wave_samp_per * cic_period;
    const double shift_min= log2(cic_n*cic_n * lo_cheat)-12;

    epicsInt32 wave_shift_temp = ceil(shift_min/2);
    if(wave_shift_temp<0)
        *wave_shift = 0;
    else
        *wave_shift = wave_shift_temp;

    *yscale = 16 * lo_cheat * (33 * wave_samp_per)*(33 * wave_samp_per) * pow(4, (8 - *wave_shift))/512.0/pow(2, shift_base);

    prec->udf = isnan(*yscale);

    return 0;
}

static
void asubFEEDRegistrar(void)
{
    registryFunctionAdd("asub_feed_timebase", (REGISTRYFUNCTION)&asub_feed_timebase);
    registryFunctionAdd("asub_split_bits", (REGISTRYFUNCTION)&asub_split_bits);
    registryFunctionAdd("sub_count_bits", (REGISTRYFUNCTION)&sub_count_bits);
    registryFunctionAdd("asub_yscale", (REGISTRYFUNCTION)&asub_yscale);
}
epicsExportRegistrar(asubFEEDRegistrar);
