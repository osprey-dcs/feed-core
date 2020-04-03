#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <math.h>

#include <errlog.h>

#include <menuFtype.h>
#include <aSubRecord.h>
#include <recGbl.h>
#include <alarm.h>
#include <dbAccess.h>

#include <epicsExport.h>
#include <registry.h>
#include <registryFunction.h>

#include <epicsMath.h>
#include <epicsTypes.h>

#include <fftw3.h>

#define PI 3.14159265359

static int rf_fft_calc(size_t N, double *in_re, double *in_im, double *out_re, double *out_im, short debug);

static
long fft_calc(aSubRecord* prec)
{
	short debug = (prec->tpro > 1) ? 1 : 0;

    size_t i;
    epicsUInt32 len = prec->nea; /* actual output length */

    double *IWF = (double*)prec->a,     /* I waveform, already scaled */
           *QWF = (double*)prec->b,     /* Q waveform, already scaled */
           TSTEP = *(double*)prec->c,   /* sample spacing [s] */
           *AFFT = (double*)prec->vala, /* amplitude FFT */
           *FWF  = (double*)prec->valb, /* frequency scale (x-axis) for FFT [Hz] */
           *AFFTMAX  = (double*)prec->vald, /* max of amplitude FFT */
           *AFFTMAXF = (double*)prec->vale; /* frequency value at FFT max amplitude */

	*AFFTMAX = 0;
	*AFFTMAXF = -1;

	double  AFFTMAX_PREV = *(double*)prec->ovld,
			AFFTMAXF_PREV = *(double*)prec->ovle;

	short *AFFTMAXFOUND = (double*)prec->valc; /* found a maximum amplitude */
	*AFFTMAXFOUND = 0;

	double threshscale = 100; /* max must be at least 100 times min */

	double afftmin;

    if(prec->fta!=menuFtypeDOUBLE 
		|| prec->ftb!=menuFtypeDOUBLE
		|| prec->ftc!=menuFtypeDOUBLE
		|| prec->ftva!=menuFtypeDOUBLE
		|| prec->ftvb!=menuFtypeDOUBLE)
    {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return 1;
    }

    if(len > prec->neb)
        len = prec->neb;
    if(len > prec->nova)
        len = prec->nova;
    if(len > prec->novb)
        len = prec->novb;

	double FFTI[len], FFTQ[len];
 
	if ( debug ) {
		printf("output length %i tstep %f s\n", len, TSTEP);
		for ( i = 0; i < 30; i++ ) {
			printf("%s convert_iq2ap: I[i] %f Q[i]] %f\n", 
				prec->name, IWF[i], QWF[i]);
		}
    }

	int rval = rf_fft_calc( len, IWF, QWF, FFTI, FFTQ, debug );

    for(i=0; i<len; i++) {
		AFFT[i] = sqrt(FFTI[i]*FFTI[i] + FFTQ[i]*FFTQ[i]);
		FWF[i] = (double)(i - len/2.0) / TSTEP / len; /* delta f = delta t / n samples */ 
		if ( i == 0 ) {
			afftmin = AFFT[i];
		}

		if ( AFFT[i] > *AFFTMAX ) {
			*AFFTMAX = AFFT[i];
			*AFFTMAXF = FWF[i];
		}
		if ( AFFT[i] < afftmin ) {
			afftmin = AFFT[i];
		}
    }

    prec->neva = prec->nevb = len;

	if ( *AFFTMAX > threshscale*afftmin ) {
		*AFFTMAXFOUND = 1;
		if ( debug ) {
			printf("found max amp %f at freq %f\n", *AFFTMAX, *AFFTMAXF );
		}
	}
	else {
		*AFFTMAX  = AFFTMAX_PREV;
		*AFFTMAXF = AFFTMAXF_PREV;
		if ( debug ) {
			printf("failed to find max amp, use prev max amp %f at freq %f\n", *AFFTMAX, *AFFTMAXF );
		}
	}
    return 0;
}

static int
rf_fft_calc(size_t N, double *in_re, double *in_im, double *out_re, double *out_im, short debug) 
{

	fftwf_complex *in, *out;
	fftwf_plan p;

	in = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * N);
	out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * N);

	int n, m, offset;

	p = fftwf_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_MEASURE);

	/* initialize input after creating plan, which overwrites the arrays when using FFTW_MEASURE */

	for ( n = 0; n < N; n++ ) {
		in[n] = in_re[n] + I*in_im[n];
		if ( debug ) {
			if ( n < 30 ) {
				printf("plan input element %i %f + i*%f\n", n, creal(in[n]), cimag(in[n]));
			}
		}
	}

	fftwf_execute(p); 

	for ( n = 0; n < N; n++ ) {

		if ( n < N/2 ) {
			offset = N/2;
		}
		else {
			offset = -N/2;
		}
		out_re[n + offset] = creal(out[n])/N; /* N/2? */
		out_im[n + offset] = cimag(out[n])/N;

		if ( debug ) {
			printf("unnormalized: plan index %i %.10f + i*%.10f adjusted index %i %.10f + i*%.10f\n", n, creal(out[n]), cimag(out[n]), n + offset, out_re[n + offset], out_im[n + offset]);
		}

	}

	fftwf_destroy_plan(p);
	fftwf_free(in); 
	fftwf_free(out);

return 0;

}

static registryFunctionRef asub_seq[] = {
    {"FFTCALC", (REGISTRYFUNCTION) &fft_calc},
};

static
void rfFFTRegister(void) {
    registryFunctionRefAdd(asub_seq, NELEMENTS(asub_seq));
}

#include <epicsExport.h>

epicsExportRegistrar(rfFFTRegister);
