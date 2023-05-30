#ifndef FFT_H
#define FFT_H

#include <epicsMutex.h>
#include <epicsMessageQueue.h>
#include <dbScan.h>

#define MAX_NAME_LENGTH 25
#define FFT_MAX_SIG 10 /* Maximum waveform signals per channel (per cavity) 
						* these will be processed by single thread
						*/
#define FFT_MAX_PLAN 2

/* Supported FFT conversion types */
#define FFT_TYPE_C2C 0 /* Complex to complex, fftwf_plan_dft_1d */
#define FFT_TYPE_R2C 1 /* Realize to complex, fftwf_plan_dft_r2c_1d*/

extern
epicsMutexId  fftInitTaskMutex; /* Global mutex for use when initializing FFT tasks */

typedef struct FFTDataRec_ {
	const char    name[MAX_NAME_LENGTH]; /* Include space for the terminating NULL */
	char          thread_name[MAX_NAME_LENGTH];
	epicsMutexId  mutex;
	size_t        fft_max_len;	        /* Maximum input/output data length for any signal */
	size_t        len[FFT_MAX_SIG];     /* Per-signal input length of last data set */
	size_t        len_output[FFT_MAX_SIG];     /* Per-signal output length of last data set */
	double        tstep[FFT_MAX_SIG];   /* Per-signal time step of last data set */
	double        fstep[FFT_MAX_SIG];   /* Per-signal frequency step of last data set */
	double       *data;	                /* FFT results, I and Q output, shared between FFT thread and device support */
	double       *freq;	                /* Frequency array for FFT output, shared between FFT thread and device support */
	epicsMessageQueueId queue_id;
} FFTDataRec, *FFTData;

typedef struct FFTMsgRec_ {
	size_t   len;    /* Data input number of elements */
	double   tstep;  /* Data time step (spacing between data points) */
	int      type;   /* FFT type, see FFT_TYPE_* above */
	int      index;  /* Index of this signal, used as index into FFTData data array */
	double   *in_re; /* Input time-domain real part */
	double   *in_im; /* Input time-domain imaginary part */
	EVENTPVT event;  /* Per-signal database scan event */
	int      debug;
} FFTMsgRec, *FFTMsg;

FFTData fftDataFind(const char *name);

int rfFFTTaskInit(char *name, size_t fft_max_len);

int fftMsgPost(epicsMessageQueueId queue_id, size_t len, double *in_re, double *in_im, double tstep, int index, EVENTPVT event, int debug, int type);


#endif /* FFT_H */
