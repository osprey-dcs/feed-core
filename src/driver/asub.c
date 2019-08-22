#include <errno.h>
#include <string.h>
#include <errlog.h>
#include <recGbl.h>
#include <alarm.h>
#include <menuFtype.h>

#include <epicsTypes.h>

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

/* Subroutine to set cavity amplitude (RFS) */
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
	max_imag  = *(double *)prec->m,
	sel_aset  = *(double *)prec->p;

    short amp_close = *(short *)prec->g,
	pha_close   = *(short *)prec->h,
	rfctrl      = *(short *)prec->n,
	rfmodectrl  = *(short *)prec->o,
	rfmodeprev  = *(short *)prec->r;
 
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

    epicsInt32 sel_lim, sel_lim_max;

    /* Outputs */
    epicsInt32 *setm = (epicsInt32 *)prec->vale,
	*lim_x_lo    = (epicsInt32 *)prec->valk,
	*lim_x_hi    = (epicsInt32 *)prec->vall,
	*lim_y_lo    = (epicsInt32 *)prec->valo,
	*lim_y_hi    = (epicsInt32 *)prec->valp;
    short *too_high  = (short *)prec->valq,
	  	*error     = (short *)prec->valt,
		*rfmodecurr= (short *)prec->valu;
    char  *msg       = (char *)prec->vals;

	*rfmodecurr = rfmodectrl;

    unsigned short *mask = (unsigned short *)prec->valr;

    short debug = (prec->tpro > 1) ? 1 : 0;

    double freqhz = freq*1e6;
    double adesv  = ades*1e6;

    unsigned short MASKOK  = 0xFF;
    unsigned short MASKERR = 0;

    *mask = MASKERR; /* Initialize to do not write registers */

/* If RF control is set off, do not push values.
 * Set error to 0, though, because this is not 
 * considered an error state and no accompanying
 * message should be necessary
 */

    if (debug) {
	printf("setAmpl: input values rfctrl %i rfmodectrl %i  prev %i ades %f MV imped %f ohms freq %f MHz qloaded %f "
		"amp_close %i pha_close %i ssa_slope %f ssa_minx %f ssa_ped %f "
		"fwd_fs %f sqrt(Watts) cav_fs %f MV mag_magn %f max_imag %f sel_aset %f\n",
		rfctrl, rfmodectrl, rfmodeprev, ades, imped, freq, qloaded, amp_close, pha_close, ssa_slope, 
		ssa_minx, ssa_ped, fwd_fs, cav_fs, max_magn, max_imag, sel_aset);
    }

	/* Pulse control. Set lim/mag registers to 0 if transitioning
	   from CW to pulsed */
	if (rfmodectrl==4) {
	*error = 0;
	*too_high = 0;
	if (rfmodeprev!=4) {
	*lim_x_lo = *lim_x_hi = *lim_y_lo = *lim_y_hi = *setm = 0;
	*mask = MASKOK;
	}
	return 0;
    }

    /* SEL raw amplitude control */
    if (rfmodectrl==3) {
	sel_lim_max = (epicsInt32)(79500 * max_magn); /* 79500 max value of lims registers */ 
	sel_lim = (epicsInt32)(floor((sel_aset/100)*79500));
	if ( sel_lim >= sel_lim_max )
	    *lim_x_lo = *lim_x_hi = sel_lim_max;
	else
	    *lim_x_lo = *lim_x_hi = sel_lim;
	*lim_y_lo = *lim_y_hi = *setm = 0;
	if ( debug ) {
	  printf("setAmpl: SEL raw mode: max lim %i sel_lim %i limxlo %i limxyhi %i limylo %i limyhi %i\n",
		 sel_lim_max, sel_lim, *lim_x_lo, *lim_x_hi, *lim_y_lo, *lim_y_hi);
	}
	  
	*error = 0;
	*too_high = 0;
	if (rfctrl == 0)
	    return 0;
	*mask = MASKOK;
	return 0;
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
	*x_lo = ssa_slope * *ssan * 0.75;
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

/* Ported from lcls2_llrf res_ctl.py. See cav_pzt.template for more info. 
 * (Resonance)
 */
static long
asub_pzt_src_set(aSubRecord *prec)
{

    /* Inputs */
    long src     = *(long *)prec->a; /* Source to set */
    long chan    = *(long *)prec->b; /* Channel(s) to set source for */
    long current = *(long *)prec->c; /* Latest reg value reading */

    /* Outputs */
    long *new = (long *)prec->vala; /* New register setting */

    /* Intermediate values */
    long mask, dismask;

    short debug = (prec->tpro > 1) ? 1 : 0;

    /* Get mask for correct source */
    switch(src) {
	case 0:
	    mask = 0x0;
	    break;
	case 1:
	    mask = 0x1;
	    break;
	case 2:
	    mask = 0x2;
	    break;
	default:
	    if (debug) {
		printf("asub_pzt_src_set: %s Source %li not recognized. "
			"Must be 0, 1, or 2.\n", prec->name, src);
	    }
	    (void)recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
	    return EINVAL;
    }

    /* Choose which piezo channel to get source */
    switch(chan) {
	case 0:
            mask = (mask << 2) | mask;
            dismask = 0xf;
	    break;
	case 1:
            mask = mask;
            dismask = 0x3;
	    break;
	case 2:
            mask = mask << 2;
            dismask = 0x3 << 2;
	    break;
	default:
	    if (debug) {
		printf("asub_pzt_src_set: %s Chan %li selection not recognized. "
			"Must be 0, 1, or 2.\n", prec->name, src);
	    }
	    (void)recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
	    return EINVAL;
    }

    /* Get current value and write-back value with correct bits set */
    *new = (~(dismask) & current) | mask;

    if (debug) {
	printf("asub_pzt_src_set: %s Source %li chan %li current %li " 
		"mask %li dismask %li new %li\n", prec->name, src, 
		chan, current, mask, dismask, *new);
    }


    return 0;
}

/* (Resonance) */
static long
asub_pzt_src_get(aSubRecord *prec)
{

    /* Inputs */
    long chan    = *(long *)prec->a; /* Channel(s) to get source for; 0 for A, 1 for B */
    long current = *(long *)prec->b; /* Latest reg value reading */

    /* Outputs */
    long *src = (long *)prec->vala; /* Current source setting for this channel */

    /* Intermediate values */
    int shift;

    short debug = (prec->tpro > 1) ? 1 : 0;

    switch(chan) {
	case 0:
	    shift = 0;
	    break;
	case 1:
	    shift = 2;
	    break;
	default:
	    if (debug) {
		printf("asub_pzt_src_get: %s Channel %li not recognized. "
			"Must be 0 for ch A or 1 for ch B.\n", prec->name, chan);
	    }
	    (void)recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
	    return EINVAL;
    }

    *src = (current & (0x3<<shift)) >> shift;

    if (debug) {
	printf("asub_pzt_src_get: %s chan %li current %li output src %li\n",
		prec->name, chan, current, *src);
    }

    return 0;
}

/* Extract unsigned integer from word (Currently used only in resonance) */
static long
asub_mask(aSubRecord *prec)
{
    epicsUInt32 input =  *(epicsUInt32 *)prec->a,
		mask   = *(epicsUInt32 *)prec->b,
		lshift = *(epicsUInt32 *)prec->c,
		rshift = *(epicsUInt32 *)prec->d;

    epicsUInt32 *output = (epicsUInt32 *)prec->vala;

    short debug = (prec->tpro > 1) ? 1 : 0;

    *output = (input & (mask << lshift)) >> rshift;

    if (debug) {
	printf("asub_mask: %s input %u mask %u lshift %u rshift %u output %u\n",
		prec->name, input, mask, lshift, rshift, *output);
	printf("asub_mask: (mask << lshift)) %u (input & (mask << lshift)) %u\n", (mask << lshift), (input & (mask << lshift)));
    }

    return 0;
}

/* Convert to signed number. (Currently used only in resonance)
 * Optionally apply linear scaling, scale and/or offset: 
 *  INPC = scale
 *  INPD = offset 
 */
static long
asub_signed(aSubRecord *prec)
{
    epicsUInt32 input =  *(epicsUInt32 *)prec->a,
		nbits =  *(epicsUInt32 *)prec->b; /* Number of bits of signed integer--
						   * counting sign bit 
						   */
    double *output = (double *)prec->vala;

    short debug = (prec->tpro > 1) ? 1 : 0;

    *output = (double)((input > (pow(2,nbits)/2 - 1)) ? input - pow(2,nbits) : input);

    if (debug) {
	printf("asub_signed: %s input %u nbits %u output %f\n",
		prec->name, input, nbits, *output);
    }

    if ( prec->ftc==menuFtypeDOUBLE ) {
	double scale = *(double *)prec->c;
	if (scale != 0) {
	    *(double *)prec->valb = *output * scale;
	    if (debug) {
		printf("asub_signed: %s applied scale factor %f new value %f\n",
		    prec->name, scale, *(double *)prec->valb);
	    }
	}
    }

    if ( prec->ftd==menuFtypeDOUBLE ) {
	double offset = *(double *)prec->d;
	if (offset != 0) {
	    *(double *)prec->valb =  *(double *)prec->valb + offset;
	    if (debug) {
		printf("asub_signed: %s applied offset %f new value %f\n",
		    prec->name, offset, *(double *)prec->valb);
	    }
	}
    }

    return 0;
}

/* Calculate quench detection coefficients (RFS) */
static long
asub_quench(aSubRecord *prec)
{
    short debug = (prec->tpro > 1) ? 1 : 0;

	double cav_scale   = *(double *)prec->a,
	       fwd_scale   = *(double *)prec->b,
	       rev_scale   = *(double *)prec->c,
	       fullscale_w = *(double *)prec->d,
	       freq_mhz    = *(double *)prec->e, /* cavity frequency */
	       imped       = *(double *)prec->f, /* shunt impedance R/Q */
	       thresh_w    = *(double *)prec->g; /* quench trip threshold */

	double *quench_const = (double *)prec->vala;
	unsigned nelm = 4, i;

    if(prec->nova != nelm) {
    	if(debug)
        	errlogPrintf("%s nova must be 4 but is %u\n", prec->name, (unsigned)prec->nova);
        (void)recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
	}

	if (debug) {
		printf("asub_quench: %s\n     inputs: cav_scale %.1f fwd_scale %.1f rev_scale %.1f\n"
			"     fullscale_w %.1f freq %.1f R/Q %.1f thresh_w %.1f\n",
			prec->name, cav_scale, fwd_scale, rev_scale, fullscale_w, freq_mhz, imped, thresh_w);
	}

    if((cav_scale<=0.0) || (fwd_scale<=0.0) || (rev_scale<=0.0) || (fullscale_w<=0.0)
		|| (freq_mhz<=0.0) || (imped<=0.0) || (thresh_w<=0.0)) {
    	if(debug)
        	errlogPrintf("%s one or more inputs is <= 0. See above.\n", prec->name);
        (void)recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        return EINVAL;
	}

	quench_const[0] = pow(rev_scale, 2) / fullscale_w;
	quench_const[1] = pow(fwd_scale, 2) / fullscale_w;

	double freq_rad = 2 * PI * freq_mhz * 1e6,
	       filter_gain = 80, /* FIR filter */
	       dt = 32 * 33 * 14 / 1320e6,
	       dudt_scale  = 16;

	double denom = freq_rad * imped * dt * filter_gain * dudt_scale;

	quench_const[2] = pow(cav_scale * 1e6, 2) / denom / fullscale_w;
	quench_const[3] = thresh_w / fullscale_w; /* normalized trip threshold */	       

	if (debug) {
		printf("     intermediate values: dt %.3e dudt_scale %.1f denom %.1f\n",
			dt, dudt_scale, denom);
    	printf("     normalized constant array %.5f %.5f %.5f %.5f\n",
			quench_const[0], quench_const[1],quench_const[2], quench_const[3]);
	}

	for (i = 0; i < nelm; i++) {
    	if(quench_const[i] >= 1.0) {
    		if(debug)
        		errlogPrintf("%s const index %i value %.5f is >= 1\n", prec->name, i, quench_const[i]);
        	(void)recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        	return EINVAL;
		}
		quench_const[i] = floor(quench_const[i] * pow(2,19));
	}

	if (debug) {
		printf("     final output array: rev %.0f fwd %.0f cav %.0f thres %.0f\n",
			quench_const[0], quench_const[1], quench_const[2], quench_const[3]);
	}

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
    registryFunctionAdd("asub_round", (REGISTRYFUNCTION)&asub_round);
    registryFunctionAdd("asub_pzt_src_set", (REGISTRYFUNCTION)&asub_pzt_src_set);
    registryFunctionAdd("asub_pzt_src_get", (REGISTRYFUNCTION)&asub_pzt_src_get);
    registryFunctionAdd("asub_mask", (REGISTRYFUNCTION)&asub_mask);
    registryFunctionAdd("asub_signed", (REGISTRYFUNCTION)&asub_signed);
    registryFunctionAdd("asub_quench_coef", (REGISTRYFUNCTION)&asub_quench);
}
epicsExportRegistrar(asubFEEDRegistrar);
