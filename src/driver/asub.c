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

    const double lo_cheat = (74762*1.646760258)/pow(2, 17);
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

/* concatinate byte array together (bytes MSBF)
 *
 * record(asub, "$(N)") {
 *   field(SNAM, "asub_feed_bcat")
 *   field(FTA, "UCHAR") # byte array
 *   field(FTVA, "ULONG")# output word
 *   field(NEA , "3")    # max number of bytes
 * }
 */
static
long asub_feed_bcat(aSubRecord *prec)
{
    const epicsUInt8 *arr = (epicsUInt8*)prec->a;
    epicsUInt32 *out = (epicsUInt32*)prec->vala;
    epicsUInt32 i, lim = prec->nea;
    /* mask includes sign bit and all higher */
    epicsUInt32 mask = 0xffffffff<<(lim*8-1);

    *out = 0;

    for(i=0; i < lim; i++) {
        *out <<= 8;
        *out |= arr[i];
    }

    /* sign extend */
    if(mask & *out)
        *out |= mask;

    return 0;
}

/* Subroutine to set cavity amplitude. Under development */
static long
asub_setamp(aSubRecord *prec)
{
    double CORDIC_SCALE = 0.774483*pow(2,17); /* From Larry */

    /* Inputs  */
    double ades   = *(double *)prec->a,
	imped     = *(double *)prec->b,
	freq      = *(double *)prec->c,
	l         = *(double *)prec->d,
	qloaded   = *(double *)prec->e,
	fwd_fs    = *(double *)prec->f,
	cav_fs    = *(double *)prec->g,
	amp_close = *(short  *)prec->h,
	ssa_slope = *(double *)prec->i,
	ssa_minx  = *(double *)prec->j,
	ssa_ped   = *(double *)prec->k,
	max_magn  = *(double *)prec->l,
	sintheta  = *(double *)prec->m,
	pha_close = *(short  *)prec->n;

/* Outputs */
    double *sqrtu = (double *)prec->vala,
	*ssa      = (double *)prec->valb, /* SSA target */
	*ssan     = (double *)prec->valc, /* Normalized SSA target */
	*adcn     = (double *)prec->vald, /* Normalized cavity ADC */
	*pol_x    = (double *)prec->valf,
	*pol_y    = (double *)prec->valg,
	*lowslope = (double *)prec->valh,
	*x_lo     = (double *)prec->vali,
	*x_hi     = (double *)prec->valj,
       	*y_lo     = (double *)prec->valm,
	*y_hi     = (double *)prec->valn,
	*max_imag = (double *)prec->valr;

    epicsInt32 *setm = (epicsInt32 *)prec->vale, /* Value for setmp reg element 0 */
	*lim_x_lo = (epicsInt32 *)prec->valk,
	*lim_x_hi = (epicsInt32 *)prec->vall,
	*lim_y_lo = (epicsInt32 *)prec->valo,
	*lim_y_hi = (epicsInt32 *)prec->valp;

    short debug = (prec->tpro > 1) ? 1 : 0;
    short *too_high = (short *)prec->valq; 

    double freqhz = freq*1e6;
    double adesv  = ades*1e6;

    double x_lo_final, x_hi_final;

    if (debug) {
	printf("setAmpl: input values ades %f MV imped %f ohms freq %f MHz l m %f qloaded %f\n",
	    ades, imped, freq, l, qloaded);
	printf("amp_close %i pha_close %i ssa_slope %f ssa_minx %f ssa_ped %f\n",
	    (int)amp_close, (int)pha_close, ssa_slope, ssa_minx, ssa_ped);
	printf("fwd_fs %f sqrt(Watts) cav_fs %f MV mag_magn %f sintheta %f\n",
	    fwd_fs, cav_fs, max_magn, sintheta);
    }

    /* Trig */
    *max_imag = max_magn * sintheta;
    *pol_y = max_magn * *max_imag;
    *pol_x = max_magn * sqrt(1 - pow(*max_imag, 2));
    /* end Trig */

    if (debug) {
	printf("setAmpl: max_magn %f max_imag %f calc policy x %f y %f\n", 
	    max_magn, *max_imag, *pol_x, *pol_y);
    }

    *sqrtu = adesv / (sqrt(imped * 2 * M_PI * freqhz));

    /* For Affine */
    *ssa = *sqrtu * sqrt((M_PI * freqhz) / (2 * qloaded));
    *ssan = *ssa / fwd_fs;

    if (debug) {
	printf("setAmpl: to affine sqrtu %f sqrt(J) ssa %f ssan %f\n", *sqrtu, *ssa, *ssan);
    }

    /* Affine */
    *lowslope = (ssa_slope * ssa_minx + ssa_ped) / ssa_minx;
    if (amp_close) { 
	*x_lo = ssa_slope * *ssan * 0.85;
	*x_hi = (ssa_slope * *ssan + ssa_ped) * 1.15;
	*x_hi = fmin(*x_hi, *lowslope * *ssan * 1.15);
    }
    else {
	*x_lo = ssa_slope * *ssan;
	*x_lo = fmin(*x_lo, *lowslope * *ssan);
	*x_hi = *x_lo;
    }

    *too_high = (*x_hi > *pol_x) ? 1 : 0;

    x_lo_final = fmin(*x_lo, *pol_x); 
    x_hi_final = fmin(*x_hi, *pol_x); 
    *lim_x_lo = (epicsInt32)(79500 * (x_lo_final));
    *lim_x_hi = (epicsInt32)(79500 * (x_hi_final));
    /* end Affine */

    /* For setmp */
    *adcn = ades / cav_fs;
    *setm = (epicsInt32)(round(*adcn * CORDIC_SCALE));

    if (debug) {
	printf("setAmpl: to setmp adcn %f setm %i\n", *adcn, *setm);
    }

    /* Gate */
    if ( pha_close ) {
	*y_lo = - *pol_y;
	*y_hi = *pol_y;
    }
    else {
	*y_lo = *y_hi = 0;
    }
    *lim_y_lo = (epicsInt32)(79500 * (*y_lo));
    *lim_y_hi = (epicsInt32)(79500 * (*y_hi));
    /* end Gate */

    return 0;
}

static
void asubFEEDRegistrar(void)
{
    registryFunctionAdd("asub_feed_timebase", (REGISTRYFUNCTION)&asub_feed_timebase);
    registryFunctionAdd("asub_split_bits", (REGISTRYFUNCTION)&asub_split_bits);
    registryFunctionAdd("sub_count_bits", (REGISTRYFUNCTION)&sub_count_bits);
    registryFunctionAdd("asub_yscale", (REGISTRYFUNCTION)&asub_yscale);
    registryFunctionAdd("asub_feed_bcat", (REGISTRYFUNCTION)&asub_feed_bcat);
    registryFunctionAdd("asub_setamp", (REGISTRYFUNCTION)&asub_setamp);
}
epicsExportRegistrar(asubFEEDRegistrar);
