#include <stdlib.h>
#include <string.h>

#include <complex.h>
#include <math.h>

#include <sys/time.h>

#include <epicsThread.h>
#include <registry.h>
#include <errlog.h>
#include <dbDefs.h>
#include <epicsExport.h>
#include <initHooks.h>

#include <fftw3.h>

#include <fftmain.h>

epicsMutexId  fftInitTaskMutex; /* Global mutex for use when initializing FFT tasks */
epicsMutexId  fftPlanMutex;  /* Global mutex for use of FFTW plan creation,
 							  * which is not thread safe
							  */

typedef struct FFTPlanRec_ {
	fftwf_plan plan;    /* Plan used for FFT transform */
	fftwf_complex *in;  /* Input array  (time domain) */
	fftwf_complex *out; /* Output array (frequency domain) */	
} FFTPlanRec, *FFTPlan;

/* just any unique address */
static void *registryId = (void*)&registryId;
static void *ioRegistryId = (void*)&ioRegistryId;
static void *ioscanRegistryId = (void*)&ioscanRegistryId;

/* Register a device's base address and return a pointer to a
 * freshly allocated FFTData struct or NULL on failure.
 */
static FFTData
fft_data_register(const char *dname)
{
char name[MAX_NAME_LENGTH];
FFTData rval = 0, d;

	sprintf( name, "fft%s", dname );

	if ( (d = malloc(sizeof(*rval) + strlen(name))) ) {

		strcpy((char*)d->name, name);
		if ( (d->mutex = epicsMutexCreate()) ) {
			/* NOTE: the registry keeps a pointer to the name and
			 *       does not copy the string, therefore we keep one.
			 *       (_must_ pass d->name, not name)
			 */
			if ( registryAdd( registryId, d->name, d ) ) {
				rval = d; d = 0;
			}
		}
	}

	if (d) {
		if (d->mutex) {
			epicsMutexDestroy(d->mutex);
		}
		free(d);
	}
	return rval;
}

/* Find the FFTData of a registered device by name */
FFTData
fftDataFind(const char *dname)
{
char name[MAX_NAME_LENGTH];

	sprintf( name, "fft%s", dname );
	return (FFTData)registryFind(registryId, name);
}

static void
rf_fft_destroy_plan(FFTPlan fftplan)
{
	epicsMutexLock( fftPlanMutex );

	fftwf_destroy_plan( fftplan->plan );
	fftwf_free( fftplan->in );
	fftwf_free( fftplan->out );

	epicsMutexUnlock( fftPlanMutex );
}

static int
rf_fft_update_plan(FFTPlan fftplan, size_t len, int new)
{
	/* Only destroy if updating pre-existing plan */
	if ( !new ) {
		rf_fft_destroy_plan( fftplan );
	}

	if ( ! (fftplan->in = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * len) ) ) {
		errlogPrintf("rf_fft_update_plan: No memory for FFT input data\n");
		return -1;
	}

	if ( ! (fftplan->out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * len) ) ) {
		errlogPrintf("rf_fft_update_plan: No memory for FFT output data\n");
		return -1;
	}

	epicsMutexLock( fftPlanMutex );

	fftplan->plan = fftwf_plan_dft_1d(len, fftplan->in, fftplan->out, FFTW_FORWARD, FFTW_MEASURE);
	epicsMutexUnlock( fftPlanMutex );

	if ( ! fftplan->plan ) {
		return -1;
	}

	return 0;
}

static FFTPlan
rf_fft_create_plan(size_t len)
{
FFTPlan fftplan = 0;

	if ( ! (fftplan = malloc( sizeof(FFTPlanRec))) ) {
		errlogPrintf("rf_fft_create_plan: No memory for FFT plan struct");
		return 0;
	}

	if ( rf_fft_update_plan( fftplan, len, 1 ) ) {
		return 0;
	}
	
	return fftplan;
}

static FFTMsg 
rf_fft_create_msg(size_t len)
{
FFTMsg msg = 0;

	if ( ! (msg = malloc( sizeof(FFTMsgRec))) ) {
		return 0;
	}

	if ( ! (msg->in_re = malloc( len * sizeof(double))) ) {
		return 0;
	}

	if ( ! (msg->in_im = malloc( len * sizeof(double))) ) {
		return 0;
	}

	return msg;
}

static int
fft_task(FFTData fftData)
{
	FFTPlan fftplan = 0;
	FFTMsg  msg = 0;

	int n, offset, index;

	double *in_re, *in_im, *out_re, *out_im; /* Local pointers */

	size_t len_current, len_max;

	epicsMessageQueueId queue_id = fftData->queue_id;

	struct timeval time_start, time_now;
	time_t  start_sec, now_sec;
	long int start_nsec, now_nsec;

 	len_current = len_max = fftData->fft_max_len;

	if ( ! (msg = rf_fft_create_msg(len_max) ) ) {
		errlogPrintf("fft_task: %s Failed to create message. Exit.\n", fftData->thread_name);
		return 0;
	}

	errlogPrintf("fft_task: %s Create first plan length %i\n", fftData->thread_name, (int)len_max);

	gettimeofday( &time_start, NULL );

	/* Creating/updating a plan is slow; this should happen rarely */
	if ( ! (fftplan = rf_fft_create_plan( len_max )) ) {
		errlogPrintf("fft_task: %s Failed to create FFT plan", fftData->thread_name);
		return 0;
	}

	gettimeofday( &time_now, NULL );
	errlogPrintf("fft_task: %s Create plan elapsed %i s %i usec\n", fftData->thread_name,
		(time_now.tv_sec - time_start.tv_sec), (long int)(time_now.tv_usec - time_start.tv_usec));

	while ( 1 ) {

		epicsMessageQueueReceive( queue_id, msg, sizeof(FFTMsgRec) );

		if ( msg->debug ) {
			errlogPrintf("fft_task: %s Msg received tstep %f len %i index %i\n", 
				fftData->thread_name, msg->tstep, (int)msg->len, msg->index);
		}

		index = msg->index;
		
		if ( (index < 0) || (index >= FFT_MAX_SIG) ) { 
			errlogPrintf("fft_task: %s Illegal data index %i\n", fftData->thread_name, index);
			continue;
		}

		if ( msg->len != len_current ) {
//			if ( msg->debug ) {
				errlogPrintf("fft_task: %s msg->len %i != len_current %i, destroy/recreate plan\n", 
					fftData->thread_name, (int)msg->len, (int)len_current);
//			}

			gettimeofday( &time_start, NULL );

			if ( rf_fft_update_plan( fftplan, msg->len, 0 ) ) {
				errlogPrintf("fft_task: %s Failed to update FFT plan\n", fftData->thread_name);
				return 0;
			}
			else if ( msg->debug ) {
				errlogPrintf("rfFFTTask: %s Created new plan of %i elements\n", fftData->thread_name, (int)msg->len);
			}

			gettimeofday( &time_now, NULL );

			errlogPrintf("fft_task: %s Update plan elapsed %i s %i usec\n",  fftData->thread_name,
				(time_now.tv_sec - time_start.tv_sec), (long int)(time_now.tv_usec - time_start.tv_usec));
		}

		len_current = msg->len;

		in_re = msg->in_re;
		in_im = msg->in_im;
		out_re = fftData->data + (index * 2 * len_max);
		out_im = fftData->data + ((index * 2 + 1) * len_max);
		if ( msg->debug ) {
			errlogPrintf("rfFFTTask: %s out_re offset %i out_im offset %i\n",
				fftData->thread_name, (int)(index * 2 * len_max), (int)((index * 2 + 1) * len_max));
		}

		epicsMutexLock ( fftData->mutex );

		fftData->len[index]   = len_current;
		fftData->tstep[index] = msg->tstep;

		/* Initialize input after creating plan (creating plan overwrites the arrays when using FFTW_MEASURE) */
		for ( n = 0; n < len_current; n++ ) {
			fftplan->in[n] = in_re[n] + I*in_im[n];
			/* temporary */
			if ( msg->debug ) {
				if ( n < 30 ) {
					printf("plan input element %i %f + i*%f\n", n, creal(fftplan->in[n]), cimag(fftplan->in[n]));
				}
			}
		}

		/* execute is thread-safe so does not need to be guarded by global mutex fftPlanMutex */
		fftwf_execute( fftplan->plan ); 

		/* FFT output is [ 0 frequency component, positive frequency components, negative frequency components ]
		 * 		out[0] = DC component
		 * 		out[1:(len/2 - 1)] = positive frequency components
		 * 		out[len/2] = Nyquist frequency
		 * 		out[len/2:len-1] = negative frequency components
		 * When extracting results to array, shift negative frequency components to beginning of array
		 */

		for ( n = 0; n < len_current; n++ ) {
			if ( n < len_current/2 ) {
				offset = len_current/2;
			}
			else {
				offset = -len_current/2;
			}
			out_re[n + offset] = creal(fftplan->out[n])/len_current;
			out_im[n + offset] = cimag(fftplan->out[n])/len_current;

			/* temporary */
			if ( msg->debug ) {
				printf("unnormalized: plan index %i %.10f + i*%.10f adjusted index %i %.10f + i*%.10f\n", 
					n, creal(fftplan->out[n]), cimag(fftplan->out[n]), n + offset, out_re[n + offset], out_im[n + offset]);
			}
		}
		epicsMutexUnlock ( fftData->mutex );

		postEvent(msg->event);
	}
}

int
fftMsgPost(epicsMessageQueueId queue_id, size_t len, double *in_re, double *in_im, double tstep, int index, EVENTPVT event, int debug)
{
	FFTMsgRec msg;

	msg.len = len;
	msg.in_re = in_re;
	msg.in_im = in_im;
	msg.tstep = tstep;
	msg.index = index;
	msg.event = event;
	msg.debug = debug;

	return epicsMessageQueueSend( queue_id, (void *)&msg, sizeof(FFTMsgRec) );
}

int
rfFFTTaskInit(char *name, size_t fft_max_len)
{
FFTData fftData = 0;
char    thread_name[MAX_NAME_LENGTH];

	if ( ! (fftData = fft_data_register(name)) ) {
		errlogPrintf("rfFFTTaskInit: %s Failed to create and register FFT data structure\n", name);
		return -1;
	}

	if ( ! (fftData->data = malloc( sizeof(double) * fft_max_len * FFT_MAX_SIG * 2 )) )  {
		errlogPrintf("rfFFTTaskInit: %s Failed to allocate memory for FFT data\n", name);
		return -1;
	}

	/* *2 for headroom, increase space for message size? */
	if ( ! (fftData->queue_id = epicsMessageQueueCreate(FFT_MAX_SIG*2, sizeof(FFTMsgRec) + (2 * sizeof(double) * fft_max_len) )) ) {
		errlogPrintf("rfFFTTaskInit: %s Failed to create message queue\n", name);
		return -1;
	}

	fftData->fft_max_len = fft_max_len;

	sprintf( thread_name, "FFT-%s", name );
	strcpy( fftData->thread_name, thread_name );

	errlogPrintf("# Create RF FFT task for %s\n", name);

	epicsThreadCreate(
		thread_name,
		epicsThreadPriorityMedium,
		epicsThreadGetStackSize(epicsThreadStackBig),
		(EPICSTHREADFUNC)fft_task,
		fftData);

	return 0;
}

static void
fftMainInit(void)
{
	fftPlanMutex     = epicsMutexCreate();
	fftInitTaskMutex = epicsMutexCreate();
}

void 
fftHook(initHookState state) {
	switch(state) {
		case initHookAtBeginning:
			fftMainInit();
			break;
		default:
			break;
	}
}

static void
fftRegistrar() {
	initHookRegister( &fftHook );
}

epicsExportRegistrar(fftRegistrar);
