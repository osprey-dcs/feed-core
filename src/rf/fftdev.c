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
	FFTData  fftData;
	int      index;    /* this record's index into FFT data array */
	EVENTPVT event;    /* database event, executed in fft_main */
	size_t   len_max;  /* maximum data length */
	epicsMessageQueueId queue_id; /* used only by sender */
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
	pvt->len_max = prec->noa;

	/* If data structure not registered, assume this is the first
	 * try and start FFT task. Then look up registered data structure.
	 * If successfully found data structure, store in dpvt.
	 */
	if ( fft_data_find( pvt, (char *)prec->desc ) ) {
		errlogPrintf("%s Initialize %s FFT task with max length %i\n",
			prec->name, prec->desc, (int)(pvt->len_max));

		epicsMutexLock( fftInitTaskMutex );
		s = rfFFTTaskInit( (char *)prec->desc, pvt->len_max );
		epicsMutexUnlock( fftInitTaskMutex );

		if ( s ) {
			errlogPrintf("%s Failed to initialize FFT task for %s\n",
				prec->name, (char *)prec->desc);
			recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
			prec->pact=TRUE;
			return -1;
		}
		if ( fft_data_find( pvt, (char *)prec->desc ) ) {
			errlogPrintf("%s Failed to find registered data structure after FFT task init\n",
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
		errlogPrintf("%s Failed to create event handle %s\n", prec->name, event_name);
		recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
		prec->pact=TRUE;
		return -1;
	}

	epicsMutexLock( pvt->fftData->mutex );
	pvt->queue_id = pvt->fftData->queue_id;
	epicsMutexUnlock( pvt->fftData->mutex );

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

	int i;
	epicsUInt32 len = prec->nea; /* actual output length */

	double  *IWF  =  (double*)prec->a,   /* I waveform, already scaled */
       		*QWF  =  (double*)prec->b,   /* Q waveform, already scaled */
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

	/* Hack: to keep data lengths consistent across all waveforms for a cavity
	 * (in order to use same FFTW plan), if len is odd, set to next lower even value.
	 * This is because the waveform lengths may be off by 1, depending on
	 * active number of waveforms. 
	 */

	if ( len % 2 != 0 ) {
		if ( debug ) {
			errlogPrintf("%s Original data length %i, reduce by one\n", 
				prec->name, len);
		}
		len -= 1;
	}

	if ( (len == 0) || (TSTEP <= 0) ) {
		if ( debug ) {
			errlogPrintf("%s Data length %i 0 or time step %f <= 0\n", 
				prec->name, len, TSTEP);
		}
		return 1;
	}

	if ( len > pvt->len_max ) {
		errlogPrintf("%s Data length %i exceeds max %i\n",
			prec->name, len, (int)pvt->len_max);
		(void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
		return 1;
	}

	/* temporary */
	if ( debug ) {
		printf("output length %i tstep %f s index %i\n", len, TSTEP, pvt->index);
		for ( i = 0; i < 30; i++ ) {
			printf("%s convert_iq2ap: I[i] %f Q[i]] %f\n", 
				prec->name, IWF[i], QWF[i]);
		}
    }

	if ( fftMsgPost( pvt->queue_id, len, IWF, QWF, TSTEP, pvt->index, pvt->event, debug ) ) {
		if ( debug ) {
			errlogPrintf("%s Error sending to FFT message queue\n", prec->name);
		}
		return 1;
	}

	return 0;
}

static long 
fft_calc_recv(aSubRecord* prec)
{
	short debug = (prec->tpro > 1) ? 1 : 0;
	FFTDev  pvt     = prec->dpvt;
	FFTData fftData = pvt->fftData;
	int index = pvt->index, /* This waveform's index into FFT data array */
	    N = 0,
		afftmax_index = 0;  /* Index of maximum amplitude */

	double *FFTI, *FFTQ; /* Local pointers */

	double *AFFT     = (double*)prec->vala, /* Amplitude FFT */
           *FWF      = (double*)prec->valb, /* Frequency scale (x-axis) for FFT [Hz] */
           *IFFT     = (double*)prec->valc, /* I FFT */
           *QFFT     = (double*)prec->vald, /* Q FFT */
           *AFFTMAX  = (double*)prec->vale, /* Max of amplitude FFT */
           *AFFTMAXF = (double*)prec->valf, /* Frequency value at FFT max amplitude */
           *AFFTMEAN = (double*)prec->valg, /* Mean of amplitude FFT */
           *FNYQ     = (double*)prec->vali, /* Max measurable freq (Nyquist), samplingrate/2 */
           *FSTEP    = (double*)prec->valj; /* FFT frequency resolution */

    size_t i;

	/* Initialize to max amplitude not found */
	*AFFTMAX  = 0.0;
	*AFFTMAXF = -1;
	*AFFTMEAN = 0.0;

	double  AFFTMAX_PREV  = *(double*)prec->ovld,
			AFFTMAXF_PREV = *(double*)prec->ovle;

	short *AFFTMAXFOUND = (short*)prec->valh; /* 1 if found a maximum amplitude */
	*AFFTMAXFOUND = 0;

	double threshscale = 10; /* Max must be at least 10 times mean (revisit this) */

    if(	prec->ftva!=menuFtypeDOUBLE
		|| prec->ftvb!=menuFtypeDOUBLE
		|| prec->ftvc!=menuFtypeDOUBLE
		|| prec->ftvd!=menuFtypeDOUBLE)
    {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return 1;
    }

	epicsMutexLock( fftData->mutex );

    epicsUInt32 len     = fftData->len[index]; /* Actual output length */
	size_t      max_len = fftData->fft_max_len;

	*FSTEP = 1 / fftData->tstep[index] / len; /* delta f = delta t / n samples */ 

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
		errlogPrintf("%s fft_re offset %i  fft_im offset %i  index %i  len %i  tstep %.4e s  fstep %f Hz\n", 
			prec->name, (int)(index * 2 * max_len), (int)((index * 2 + 1) * max_len), index, (int)len, 
			fftData->tstep[index], *FSTEP);
	}
 
    for(i=0; i<len; i++) {
		IFFT[i] = FFTI[i];
		QFFT[i] = FFTQ[i];
		AFFT[i] = sqrt(FFTI[i]*FFTI[i] + FFTQ[i]*FFTQ[i]);
		FWF[i]  = (double)(i - len/2.0) * *FSTEP;

		if ( AFFT[i] > *AFFTMAX ) {
			*AFFTMAX = AFFT[i];
			*AFFTMAXF = FWF[i];
			afftmax_index = i;
		}

        *AFFTMEAN += AFFT[i];
        N++;
    }

	/* Nyquist frequency, maximum measurable frequency given time step */
	*FNYQ = 1 / fftData->tstep[index] / 2.0;

	epicsMutexUnlock( fftData->mutex );

    prec->neva = prec->nevb = prec->nevc = prec->nevd = len;

    if( N == 0 ) {
        recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        return 0;
    }
    *AFFTMEAN  /= N; // <x>


	if ( *AFFTMAX > threshscale * *AFFTMEAN ) {
		*AFFTMAXFOUND = 1;
		if ( debug ) {
			errlogPrintf("%s Found max amp %f at freq %f mean %f index %i\n", 
				prec->name, *AFFTMAX, *AFFTMAXF, *AFFTMEAN, afftmax_index );
		}
	}
	else {
		*AFFTMAX  = AFFTMAX_PREV;
		*AFFTMAXF = AFFTMAXF_PREV;
		if ( debug ) {
			errlogPrintf("%s Failed to find max amp, use prev max amp %f at freq %f, current mean %f\n", 
				prec->name, *AFFTMAX, *AFFTMAXF, *AFFTMEAN );
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
void asubFFTRegister(void) {
    registryFunctionRefAdd(asub_seq, NELEMENTS(asub_seq));
}

#include <epicsExport.h>

epicsExportRegistrar(asubFFTRegister);
