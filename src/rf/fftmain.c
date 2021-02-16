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
epicsMutexId  fftPlanMutex;     /* Global mutex for use of FFTW plan creation,
 							     * which is not thread safe
							     */

/* Two types of FFTs:
 *  - complex-to-complex
 *  - real-to-complex
 * Assumptions/requirements
 *  - Maximum input data lengths are the same
 */
typedef struct FFTPlanRec_ {
	fftw_plan plan;    /* Plan used for FFT transform */
/* input array used for complex-to-complex */
	fftw_complex *in;  /* Input array  (time domain) */
/* input arrays used for real-to-complex */
	double *in_re;  /* Real input array */
/* output array */
	fftw_complex *out; /* Output array (frequency domain) */	
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
rf_fft_destroy_plan(FFTPlan fftplan, int type)
{
	epicsMutexLock( fftPlanMutex );

	fftw_destroy_plan( fftplan->plan );

	switch (type) {

		case FFT_TYPE_C2C:
			fftw_free( fftplan->in );
			fftw_free( fftplan->out );
			break;
		case FFT_TYPE_R2C:
			fftw_free( fftplan->in_re );
			fftw_free( fftplan->out );
			break;
	}

	epicsMutexUnlock( fftPlanMutex );
}

static int
rf_fft_update_plan_c2c(FFTPlan fftplan, size_t len)
{
	if ( ! (fftplan->in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * len) ) ) {
		errlogPrintf("rf_fft_update_plan_c: No memory for FFT input data\n");
		return -1;
	}

	if ( ! (fftplan->out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * len) ) ) {
		errlogPrintf("rf_fft_update_plan_c: No memory for FFT output data\n");
		return -1;
	}

	epicsMutexLock( fftPlanMutex );

	fftplan->plan = fftw_plan_dft_1d(len, fftplan->in, fftplan->out, FFTW_FORWARD, FFTW_MEASURE);

	epicsMutexUnlock( fftPlanMutex );

	return 0;
}

static int
rf_fft_update_plan_r2c(FFTPlan fftplan, size_t len)
{
	if ( ! (fftplan->in_re = (double*) fftw_malloc(sizeof(double) * len) ) ) {
		errlogPrintf("rf_fft_update_plan_r: No memory for FFT input data\n");
		return -1;
	}

	if ( ! (fftplan->out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * len) ) ) {
		errlogPrintf("rf_fft_update_plan_r: No memory for FFT output data\n");
		return -1;
	}

	epicsMutexLock( fftPlanMutex );

	fftplan->plan = fftw_plan_dft_r2c_1d(len, fftplan->in_re, fftplan->out, FFTW_MEASURE);

	epicsMutexUnlock( fftPlanMutex );

	return 0;
}

static int
rf_fft_update_plan(FFTPlan fftplan, size_t len, int new, int type)
{
	/* Only destroy if updating pre-existing plan */
	if ( !new ) {
		rf_fft_destroy_plan( fftplan, type );
	}

	switch (type) {

		case FFT_TYPE_C2C:
			if ( rf_fft_update_plan_c2c( fftplan, len ) ) {
				return -1;
			}
			break;

		case FFT_TYPE_R2C:
			if ( rf_fft_update_plan_r2c( fftplan, len ) ) {
				return -1;
			}
			break;
	}

	if ( ! fftplan->plan ) {
		return -1;
	}

	return 0;
}

static FFTPlan
rf_fft_create_plan(size_t len, int type)
{
FFTPlan fftplan = 0;

	if ( ! (fftplan = malloc( sizeof(FFTPlanRec))) ) {
		errlogPrintf("rf_fft_create_plan: No memory for FFT plan struct type %i", type);
		return 0;
	}

	if ( rf_fft_update_plan( fftplan, len, 1, type ) ) {
		return 0;
	}

	return fftplan;
}

/*
 * complex to complex transform
 *
 * len - input data number of elements
 * tstep - time step size
 * in_re, in_im - input data arrays (time domain)
 * out_re, out_im - output data arrays (frequency domain)
 * out_freq - frequency steps corresponding to output data
 * len_output - output data number of elements (same as input for complex-to-complex)
 * fstep - frequency step size
 */

static void
rf_fft_execute_plan_c2c(FFTPlan fftplan, size_t len, double tstep, int debug, double *in_re, double *in_im, double *out_re, double *out_im, double *out_freq, size_t *len_output, double *fstep)
{
int n, offset;

	*len_output = len;

	*fstep = 1 / tstep / len; /* delta f = delta t / n samples */ 

	/* Initialize input after creating plan (creating plan overwrites the arrays when using FFTW_MEASURE) */
	for ( n = 0; n < len; n++ ) {
		fftplan->in[n] = in_re[n] + I*in_im[n];
	}

	/* execute is thread-safe so does not need to be guarded by global mutex fftplanMutex */
	fftw_execute( fftplan->plan ); 

	/* FFT output is [ 0 frequency component, positive frequency components, negative frequency components ]
	 * 		out[0] = DC component
	 * 		out[1:(len/2 - 1)] = positive frequency components
	 * 		out[len/2] = Nyquist frequency
	 * 		out[len/2:len-1] = negative frequency components
	 * When extracting results to array, shift negative frequency components to beginning of array
	 */

	for ( n = 0; n < len; n++ ) {
		if ( n < len/2 ) {
			offset = len/2;
		}
		else {
			offset = -len/2;
		}
		out_re[n + offset] = creal(fftplan->out[n])/len;
		out_im[n + offset] = cimag(fftplan->out[n])/len;

		out_freq[n]  = (double)(n - len/2.0) * *fstep;

		/* temporary */
		if ( debug ) {
			errlogPrintf("unnormalized: plan index %i %.10f + i*%.10f adjusted index %i %.10f + i*%.10f\n", 
				n, creal(fftplan->out[n]), cimag(fftplan->out[n]), n + offset, out_re[n + offset], out_im[n + offset]);
		}
	}
}

/*
 * real to complex transform
 *
 * len - input data number of elements
 * tstep - time step size
 * in_re - input data array (real only, time domain)
 * out_re, out_im - output data arrays (frequency domain)
 * out_freq - frequency steps corresponding to output data
 * len_output - output data number of elements
 * fstep - frequency step size
 */

static void
rf_fft_execute_plan_r2c(FFTPlan fftplan, size_t len, double tstep, int debug, double *in_re, double *out_re, double *out_im, double *out_freq, size_t *len_output, double *fstep)
{
int n;

	*len_output = len/2 + 1;

	*fstep = 1 / tstep / len; /* delta f = delta t / n samples */ 

	/* temporary */
	if ( debug ) {
		errlogPrintf("r2r execute, len_output %i fstep %.1f Hz\n", (int)(*len_output), *fstep); 
	}

	/* Initialize input after creating plan (creating plan overwrites the arrays when using FFTW_MEASURE) */
	for ( n = 0; n < len; n++ ) {
		fftplan->in_re[n] = in_re[n];
	}

	/* execute is thread-safe so does not need to be guarded by global mutex fftplanMutex */
	fftw_execute( fftplan->plan ); 

	/* FFT output is (len/2 + 1) complex elements
	 * Per FFTW, 
	 * 		out[0] = DC component, purely real
	 * 		out[len/2] = Nyquist frequency when n is even, purely real--but in output,
	 *         it is (len/2+1)th element that shows this
	 */

	for ( n = 0; n < *len_output; n++ ) {

		/* check normalization */
		out_re[n] = creal(fftplan->out[n])/len;
		out_im[n] = cimag(fftplan->out[n])/len;

		out_freq[n]  = (double)n * *fstep;

		/* temporary */
		if ( debug ) {
			errlogPrintf("unnormalized: plan index %i %.10f output %.10f +i %.10f freq %.1f\n", 
				n, fftplan->in_re[n], out_re[n], out_im[n], out_freq[n]);
		}
	}
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

/* On task startup, create initial FFTW plans, using maximum input data lengths
 *     If this fails, exit task
 * During task run-time, loop forever, waiting for messages
 *     Received messages contain input time domain data +
 *        various meta data: FFT conversion type, actual input data length,
 *                           input data time step
 *     When receive message, execute FFT plan of specified type,
 *        send reply message containing frequency domain data, length of 
 *        that data, array of frequency steps, frequency time step
 *     If received message of conversion type N input data length does not 
 *        match the current plan, update the plan. This should happen rarely.
 *
 */
static int
fft_task(FFTData fftData)
{
	FFTPlan fftplans[FFT_MAX_PLAN] = { 0, 0 };

	FFTMsg  msg = 0;

	int index, type, i;

	double *in_re, *in_im, *out_re, *out_im, *out_freq; /* Local pointers */

	double fstep = 0;

	size_t len_current[FFT_MAX_PLAN], len_max, len_output = 0;

	epicsMessageQueueId queue_id = fftData->queue_id;

	struct timeval time_start, time_now;

	len_max = fftData->fft_max_len;
	if ( ! (msg = rf_fft_create_msg(len_max) ) ) {
		errlogPrintf("fft_task: %s Failed to create message. Exit.\n", fftData->thread_name);
		return 0;
	}

	/* errlogPrintf("fft_task: %s Create first plan length %i\n", fftData->thread_name, (int)len_max);
	gettimeofday( &time_start, NULL ); */

	/* Creating/updating a plan is slow; this should happen rarely */
	for ( i = 0; i < FFT_MAX_PLAN; i++ ) {
		len_current[i] = len_max;
		if ( ! (fftplans[i] = rf_fft_create_plan( len_current[i], i )) ) {
			errlogPrintf("fft_task: %s Failed to create FFT plan %i", fftData->thread_name, i);
			return 0;
		}	
	}
	/*gettimeofday( &time_now, NULL );
	errlogPrintf("fft_task: %s Create plan elapsed %i s %i usec\n", fftData->thread_name,
		(time_now.tv_sec - time_start.tv_sec), (long int)(time_now.tv_usec - time_start.tv_usec)); */

	while ( 1 ) {

		epicsMessageQueueReceive( queue_id, msg, sizeof(FFTMsgRec) );

		type = msg->type;
		index = msg->index;

		if ( msg->debug ) {
			errlogPrintf("fft_task: %s Msg received tstep %f len %i index %i type %i\n", 
				fftData->thread_name, msg->tstep, (int)msg->len, index, type);
		}
		
		if ( (index < 0) || (index >= FFT_MAX_SIG) ) { 
			errlogPrintf("fft_task: %s Illegal data index %i\n", fftData->thread_name, index);
			continue;
		}

		if ( msg->len != len_current[type] ) {
			if ( msg->debug ) {
				errlogPrintf("fft_task: %s msg->len %i != len_current %i, destroy/recreate plan\n", 
					fftData->thread_name, (int)msg->len, (int)len_current[type]);
				gettimeofday( &time_start, NULL );
			}

			if ( rf_fft_update_plan( fftplans[type], msg->len, 0, type ) ) {
				errlogPrintf("fft_task: %s Failed to update FFT plan type %i\n", fftData->thread_name, type);
				return 0;
			}
			else if ( msg->debug ) {
				errlogPrintf("rfFFTTask: %s Created new plan of %i elements\n", fftData->thread_name, (int)msg->len);
			}

			if ( msg->debug ) {
				gettimeofday( &time_now, NULL );
				errlogPrintf("fft_task: %s Update plan elapsed %i s %i usec\n",  fftData->thread_name,
					(int)(time_now.tv_sec - time_start.tv_sec), (int)(time_now.tv_usec - time_start.tv_usec));
			}
		}

		len_current[type] = msg->len;

		in_re = msg->in_re;
		in_im = msg->in_im;
		out_re = fftData->data + (index * 2 * len_max);
		out_im = fftData->data + ((index * 2 + 1) * len_max);
		out_freq = fftData->freq + (index * len_max);
		if ( msg->debug ) {
			errlogPrintf("rfFFTTask: %s out_re offset %i out_im offset %i\n",
				fftData->thread_name, (int)(index * 2 * len_max), (int)((index * 2 + 1) * len_max));
		}

		epicsMutexLock ( fftData->mutex );

		switch (type) {

			case FFT_TYPE_C2C:
				rf_fft_execute_plan_c2c( fftplans[FFT_TYPE_C2C], len_current[type], msg->tstep, msg->debug, in_re, in_im, out_re, out_im, out_freq, &len_output, &fstep);
				break;

			case FFT_TYPE_R2C:
				rf_fft_execute_plan_r2c( fftplans[FFT_TYPE_R2C], len_current[type], msg->tstep, msg->debug, in_re, out_re, out_im, out_freq, &len_output, &fstep);
				break;
		}


		fftData->len[index]   = len_current[type]; /* input data length  */
		fftData->len_output[index] = len_output;   /* output data length */
		fftData->fstep[index] = fstep;
		fftData->tstep[index] = msg->tstep;

		epicsMutexUnlock ( fftData->mutex );

		postEvent(msg->event);
	}
}

int
fftMsgPost(epicsMessageQueueId queue_id, size_t len, double *in_re, double *in_im, double tstep, int index, EVENTPVT event, int debug, int type)
{
	FFTMsgRec msg;

	msg.len = len;
	msg.in_re = in_re;
	msg.in_im = in_im;
	msg.tstep = tstep;
	msg.index = index;
	msg.event = event;
	msg.debug = debug;
	msg.type = type;

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

	if ( ! (fftData->freq = malloc( sizeof(double) * fft_max_len * FFT_MAX_SIG )) )  {
		errlogPrintf("rfFFTTaskInit: %s Failed to allocate memory for FFT frequencies array\n", name);
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
