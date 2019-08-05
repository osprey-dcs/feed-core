#include <errno.h>
#include <string.h>
#include <errlog.h>
#include <recGbl.h>
#include <alarm.h>
#include <menuFtype.h>

#include <subRecord.h>
#include <aSubRecord.h>

#include <registryFunction.h>
#include <epicsExport.h>

#include <epicsMath.h>

#define PI 3.14159265359

#define MIN(A,B) ((A)<(B) ? (A) : (B))
#define MAX(A,B) ((A)>(B) ? (A) : (B))

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

    if(step==0.0)
        step = 1.0;

    if(prec->ftd==menuFtypeDOUBLE && prec->ned>=0) {
        double div = *(double*)prec->d;
        if(div!=0.0)
            step *= div;
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
    field(SNAM, "asub_split_bits")
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

/* parse <=32 bit mask (alternative to asub_split_bits)
 *
record(sub, "$(N)") {
    field(SNAM, "sub_feed_nset_bits")
    field(INPA, "mybit") # bit index
    field(INPB, "bitmask") # search in this bitmask
}
 * For bit index A:
 *  if A is not set in B, result is -1.
 *  if A is set, then result is number of set bits in B with an index less than B.
 *
 * eg.
 *  A=2 and B=5 (0b0111) yields 2
 *  A=2 and B=5 (0b0101) yields 1
 *  A=2 and B=4 (0b0100) yields 0
 *  A=2 and B=1 (0b0001) yields -1
 *  A=2 and B=8 (0b1000) yields -1
 */
static
long sub_feed_nset_bits(subRecord *prec)
{
    epicsUInt32 mybit = (unsigned long)prec->a;
    epicsUInt32 mymask = 1u<<mybit;
    epicsUInt32 bitmask = (unsigned long)prec->b;
    epicsInt32 nbits = -1;

    /* only count if mybit is set */
    if(bitmask & mymask) {
        /* count mybit, and lower */
        epicsUInt32 countmask = bitmask & (mymask | (mymask-1));

        for(;countmask; countmask>>=1) {
            if(countmask&1u)
                nbits++;
        }
    }

    prec->val = nbits;
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
    const epicsUInt32   cic_n    = wave_samp_per * cic_period;
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

/* Subroutine to set cavity amplitude */
static 
long asub_setamp(aSubRecord *prec)
{
    double CORDIC_SCALE = 0.774483*pow(2,17);

    /* Inputs  */
    double ades   = *(double *)prec->a,
	imped     = *(double *)prec->b,
	freq      = *(double *)prec->c,
	qloaded   = *(double *)prec->d,
	fwd_fs    = *(double *)prec->e,
	cav_fs    = *(double *)prec->f,
	ssa_slope = *(double *)prec->i,
	ssa_minx  = *(double *)prec->j,
	ssa_ped   = *(double *)prec->k,
	max_magn  = *(double *)prec->l,
	max_imag  = *(double *)prec->m;

    short amp_close = *(short *)prec->g,
	pha_close   = *(short *)prec->h,
	rfctrl      = *(short *)prec->n;

    /* Intermediate results */
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
	*y_hi     = (double *)prec->valn;
    double x_lo_final, x_hi_final;

    /* Outputs */
    epicsInt32 *setm = (epicsInt32 *)prec->vale,
	*lim_x_lo    = (epicsInt32 *)prec->valk,
	*lim_x_hi    = (epicsInt32 *)prec->vall,
	*lim_y_lo    = (epicsInt32 *)prec->valo,
	*lim_y_hi    = (epicsInt32 *)prec->valp;
    short *too_high  = (short *)prec->valq,
	  *error     = (short *)prec->valt;
    char  *msg       = (char *)prec->vals;

    unsigned short *mask = (unsigned short *)prec->valr;

    short debug = (prec->tpro > 1) ? 1 : 0;

    double freqhz = freq*1e6;
    double adesv  = ades*1e6;

    unsigned short MASKOK  = 0xFF;
    unsigned short MASKERR = 0;

    if (debug) {
	printf("setAmpl: input values ades %f MV imped %f ohms freq %f MHz qloaded %f "
		"amp_close %i pha_close %i ssa_slope %f ssa_minx %f ssa_ped %f "
		"fwd_fs %f sqrt(Watts) cav_fs %f MV mag_magn %f max_imag %f\n",
		ades, imped, freq, qloaded, amp_close, pha_close, ssa_slope, 
		ssa_minx, ssa_ped, fwd_fs, cav_fs, max_magn, max_imag);
    }

    /* Policy maximum X/Y */
    *pol_y = max_magn * max_imag;
    *pol_x = max_magn * sqrt(1 - pow(max_imag, 2));

    if (debug) {
	printf("setAmpl: max_magn %f max_imag %f calc policy x %f y %f\n", 
	    max_magn, max_imag, *pol_x, *pol_y);
    }

    /* Cavity sqrt(energy) 
     * V/(sqrt( (shunt impedance) * 2pi * (cav freq)))
     */
    *sqrtu = adesv / (sqrt(imped * 2 * PI * freqhz));

    /* Target SSA ADC normalized amplitude */
    *ssa = *sqrtu * sqrt((PI * freqhz) / (2 * qloaded));
    *ssan = *ssa / fwd_fs;

    if (debug)
	printf("setAmpl: to affine sqrtu %f sqrt(J) ssa %f ssan %f\n", *sqrtu, *ssa, *ssan);

    /* Calculate values for X limit registers */
    *lowslope = (ssa_slope * ssa_minx + ssa_ped) / ssa_minx;
    if (amp_close) { 
	*x_lo = ssa_slope * *ssan * 0.85;
	*x_hi = (ssa_slope * *ssan + ssa_ped) * 1.15;
	*x_hi = MIN(*x_hi, *lowslope * *ssan * 1.15);
    }
    else {
	*x_lo = ssa_slope * *ssan;
	*x_lo = MIN(*x_lo, *lowslope * *ssan);
	*x_hi = *x_lo;
    }
    *too_high = (*x_hi > *pol_x) ? 1 : 0;
    x_lo_final = MIN(*x_lo, *pol_x); 
    x_hi_final = MIN(*x_hi, *pol_x); 
    *lim_x_lo = (epicsInt32)(79500 * (x_lo_final));
    *lim_x_hi = (epicsInt32)(79500 * (x_hi_final));

    /* Calculate value for set-magnitude register */
    *adcn = ades / cav_fs;
    *setm = (epicsInt32)(round(*adcn * CORDIC_SCALE));

    if (debug)
	printf("setAmpl: to setmp adcn %f setm %i\n", *adcn, *setm);

    /* Calculate values for Y limit registers */
    *y_lo = *y_hi = 0;
    if ( pha_close && (ades>0.0) ) {
	*y_lo = - *pol_y;
	*y_hi = *pol_y;
    }
    *lim_y_lo = (epicsInt32)(79500 * (*y_lo));
    *lim_y_hi = (epicsInt32)(79500 * (*y_hi));

    *mask = MASKERR; /* Initialize to do not write registers */

    /* If RF control is set off, do not push values.
     * Set error to 0, though, because this is not 
     * considered an error state and no accompanying
     * message should be necessary
     */
    *error = 0;
    if (rfctrl == 0)
	return 0;

    /* Determine if settings should actually be written to registers.
     * TODO: Revisit numbers used in cav/fwd scale checks
     */
    if (*too_high) {
	if (cav_fs < 25)
	    sprintf(msg, "Overrange. Check cav scale");
	else if (fwd_fs < 50)
	    sprintf(msg, "Overrange. Check fwd scale");
	else 
	    sprintf(msg, "Overrange");
	*error = 1;
	return 0;
    }
    else if (isnan(*lowslope) || isinf(*lowslope)) {
	sprintf(msg, "Bad lowslope. Check SSA parms");
	*error = 1;
	return 0;
    }

    *mask = MASKOK;
    return 0;
}

static long
asub_round(aSubRecord *prec)
{
    double input = *(double *)prec->a;
    long *output = (long *)prec->vala;

    *output = (long)(round(input));
    
    return 0;

}

static
void asubFEEDRegistrar(void)
{
    registryFunctionAdd("asub_feed_timebase", (REGISTRYFUNCTION)&asub_feed_timebase);
    registryFunctionAdd("asub_split_bits", (REGISTRYFUNCTION)&asub_split_bits);
    registryFunctionAdd("sub_count_bits", (REGISTRYFUNCTION)&sub_count_bits);
    registryFunctionAdd("asub_yscale", (REGISTRYFUNCTION)&asub_yscale);
    registryFunctionAdd("sub_feed_nset_bits", (REGISTRYFUNCTION)&sub_feed_nset_bits);
    registryFunctionAdd("asub_feed_bcat", (REGISTRYFUNCTION)&asub_feed_bcat);
    registryFunctionAdd("asub_setamp", (REGISTRYFUNCTION)&asub_setamp);
    registryFunctionAdd("asub_round", (REGISTRYFUNCTION)&asub_round);
}
epicsExportRegistrar(asubFEEDRegistrar);
