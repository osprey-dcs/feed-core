#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>

#include <epicsExport.h>
#include <registry.h>
#include <registryFunction.h>
#include <alarm.h>
#include <recGbl.h>
#include <errlog.h>
#include <aSubRecord.h>
#include <dbScan.h>
#include <epicsTypes.h>
#include <menuFtype.h>

#include <fftmain.h>

/* Note on use of fftTaskInitMutex (devmain.h):
 * Each task supports a single RFS shell and its waveform signals.
 * The first asub record initializes its associated task, so lock
 * to prevent a second record from doing so while that
 * task initialization is incomplete.
 * Fine to use one mutex for all because each asub uses it at most
 * once, at record init.
 */

/* Data private device support; to be stored in record's DPVT field */
typedef struct FFTDev_ {
	FFTData fftData;
	int     index; /* this record's index into FFT data array */
	EVENTPVT event; /* database event, executed in fft_main */
} FFTDevRec, *FFTDev;

static long
fft_data_find(FFTDev pvt, char *name) 
{
	FFTData fftData = 0;

	if ( ! (fftData = fftDataFind( name )) ) {
		return -1;
	}
	pvt->fftData = fftData;
	return 0;
}

static long 
fft_sub_init(aSubRecord* prec)
{
	FFTDev pvt = 0; /* Info stored with record */
	int    s;

    if(prec->ftm != menuFtypeLONG) {
        errlogPrintf("%s: FTM must be LONG\n", prec->name);
        return -1;
    }

	if ( ! (pvt = malloc( sizeof( FFTDevRec ) )) ) {
		errlogPrintf("%s No memory for record data structure", prec->name);
		return -1;
	}

	pvt->index = *(epicsInt32 *)prec->m;

	/* If data structure not registered, assume this is the first
	 * try and start FFT task. Then look up registered data structure.
	 * If successfully found data structure, store in dpvt.
	 */
	if ( fft_data_find( pvt, (char *)prec->desc ) ) {
		errlogPrintf("%s Initialize %s FFT task with max length %i\n",
			prec->name, prec->desc, prec->noa);

		epicsMutexLock( fftInitTaskMutex );
		s = rfFFTTaskInit( (char *)prec->desc, prec->noa );
		epicsMutexLock( fftInitTaskMutex );

		if ( s ) {
			errlogPrintf("%s failed to initialize FFT task for %s\n",
				prec->name, (char *)prec->desc);
			recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
			prec->pact=TRUE;
			return -1;
		}
		if ( fft_data_find( pvt, (char *)prec->desc ) ) {
			errlogPrintf("%s Failed to find registered data structure after initializing FFT task\n",
				prec->name);
			recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
			prec->pact=TRUE;
			return -1;
		}
	}

	prec->dpvt = pvt;

	return 0;
}

static long 
fft_init_send(aSubRecord* prec)
{
	char event_name[MAX_NAME_LENGTH];
	FFTDev pvt;

	if ( fft_sub_init( prec ) ) {
		recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
		prec->pact=TRUE;
		return -1;
	}
	pvt = prec->dpvt;

	sprintf( event_name, "event-fft-%s-%i", (char *)prec->desc, pvt->index );
	if ( 0 == (pvt->event = eventNameToHandle( event_name )) ) {
		errlogPrintf("%s failed to create event handle %s\n", prec->name, event_name);
		recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
		prec->pact=TRUE;
		return -1;
	}

	return 0;
}

static long 
fft_init_recv(aSubRecord* prec)
{
	if ( fft_sub_init( prec ) ) {
		recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
		prec->pact=TRUE;
		return -1;
	}
	
	return 0;
}

static long 
fft_calc_send(aSubRecord* prec)
{
	short   debug   = (prec->tpro > 1) ? 1 : 0;
	FFTDev  pvt     = prec->dpvt;
	FFTData fftData = pvt->fftData;

	int i;
	epicsUInt32 len = prec->nea; /* actual output length */

	double  *IWF = (double*)prec->a,     /* I waveform, already scaled */
       		*QWF = (double*)prec->b,     /* Q waveform, already scaled */
       		TSTEP = *(double*)prec->c;   /* sample spacing [s] */
 
    if(prec->fta!=menuFtypeDOUBLE 
		|| prec->ftb!=menuFtypeDOUBLE
		|| prec->ftc!=menuFtypeDOUBLE)
    {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return 1;
    }

    if(len > prec->neb)
        len = prec->neb;

	if ( debug ) {
		printf("output length %i tstep %f s index %i\n", len, TSTEP, pvt->index);
		for ( i = 0; i < 30; i++ ) {
			printf("%s convert_iq2ap: I[i] %f Q[i]] %f\n", 
				prec->name, IWF[i], QWF[i]);
		}
    }

	if ( debug ) {
		printf("%s fft_calc_send: before post msg index %i len %i\n", prec->name, pvt->index, len);
	}

	if ( fftMsgPost( fftData->queue_id, len, IWF, QWF, TSTEP, pvt->index, pvt->event, debug ) ) {
		if ( debug ) {
			printf("%s error sending to FFT message queue\n", prec->name);
		}
		return 1;
	}

	return 0;
}

static long 
fft_calc_recv(aSubRecord* prec)
{
	short debug = (prec->tpro > 1) ? 1 : 0;
	FFTDev pvt = prec->dpvt;
	FFTData fftData = pvt->fftData;
	int index = pvt->index, 
	N = 0;

	double *FFTI, *FFTQ; /* local pointers */

	double *AFFT = (double*)prec->vala, /* amplitude FFT */
           *FWF  = (double*)prec->valb, /* frequency scale (x-axis) for FFT [Hz] */
           *IFFT  = (double*)prec->valc,    /* I FFT */
           *QFFT  = (double*)prec->vald,    /* I FFT */
           *AFFTMAX  = (double*)prec->vale, /* max of amplitude FFT */
           *AFFTMAXF = (double*)prec->valf, /* frequency value at FFT max amplitude */
           *AFFTMEAN = (double*)prec->valg, /* mean of amplitude FFT */
           *FNYQ     = (double*)prec->vali; /* max measurable freq (Nyquist), samplingrate/2 */

    size_t i;

	*AFFTMAX = 0.0;
	*AFFTMAXF = -1;
	*AFFTMEAN = 0.0;

	double  AFFTMAX_PREV = *(double*)prec->ovld,
			AFFTMAXF_PREV = *(double*)prec->ovle;

	short *AFFTMAXFOUND = (short*)prec->valh; /* found a maximum amplitude */
	*AFFTMAXFOUND = 0;

	double threshscale = 10; /* max must be at least 10 times mean (revisit this) */

    if(	prec->ftva!=menuFtypeDOUBLE
		|| prec->ftvb!=menuFtypeDOUBLE
		|| prec->ftvc!=menuFtypeDOUBLE
		|| prec->ftvd!=menuFtypeDOUBLE)
    {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return 1;
    }

	epicsMutexLock( fftData->mutex );

    epicsUInt32 len     = fftData->len[index]; /* actual output length */
	size_t      max_len = fftData->fft_max_len;

    if(len > prec->nova)
        len = prec->nova;
    if(len > prec->novb)
        len = prec->novb;
    if(len > prec->novc)
        len = prec->novc;
    if(len > prec->novd)
        len = prec->novd;

	FFTI = fftData->data + (index * 2 * max_len);
	FFTQ = fftData->data + ((index * 2 + 1) * max_len);

	if ( debug ) {
		printf("fft_calc_recv: %s fft_re offset %i fft_im offset %i index %i len %i tstep %f\n", 
			prec->name, (int)(index * 2 * max_len), (int)((index * 2 + 1) * max_len), index, (int)len, fftData->tstep[index]);
	}
 
    for(i=0; i<len; i++) {
		IFFT[i] = FFTI[i];
		QFFT[i] = FFTQ[i];
		AFFT[i] = sqrt(FFTI[i]*FFTI[i] + FFTQ[i]*FFTQ[i]);
		FWF[i]  = (double)(i - len/2.0) / fftData->tstep[index] / len; /* delta f = delta t / n samples */ 

		if ( AFFT[i] > *AFFTMAX ) {
			*AFFTMAX = AFFT[i];
			*AFFTMAXF = FWF[i];
		}

        *AFFTMEAN += AFFT[i];
        N++;
    }

	*FNYQ = 1 / fftData->tstep[index] / 2.0;

	epicsMutexUnlock( fftData->mutex );

    if( N == 0 ) {
        recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        return 0;
    }
    *AFFTMEAN  /= N; // <x>

    prec->neva = prec->nevb = prec->nevc = prec->nevd = len;

	if ( *AFFTMAX > threshscale * *AFFTMEAN ) {
		*AFFTMAXFOUND = 1;
		if ( debug ) {
			printf("found max amp %f at freq %f mean %f\n", *AFFTMAX, *AFFTMAXF, *AFFTMEAN );
		}
	}
	else {
		*AFFTMAX  = AFFTMAX_PREV;
		*AFFTMAXF = AFFTMAXF_PREV;
		if ( debug ) {
			printf("failed to find max amp, use prev max amp %f at freq %f, current mean %f\n", *AFFTMAX, *AFFTMAXF, *AFFTMEAN );
		}
	}
    return 0;
}

static registryFunctionRef asub_seq[] = {
    {"FFTSENDINIT", (REGISTRYFUNCTION) &fft_init_send},
    {"FFTCALCINIT", (REGISTRYFUNCTION) &fft_init_recv},
    {"FFTSEND",     (REGISTRYFUNCTION) &fft_calc_send},
    {"FFTCALC",     (REGISTRYFUNCTION) &fft_calc_recv},
};

static
void rfFFTRegister(void) {
    registryFunctionRefAdd(asub_seq, NELEMENTS(asub_seq));
}

#include <epicsExport.h>

epicsExportRegistrar(rfFFTRegister);
