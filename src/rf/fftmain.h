#ifndef FFT_H
#define FFT_H

#include <epicsMutex.h>
#include <epicsMessageQueue.h>
#include <dbScan.h>

#define MAX_NAME_LENGTH 25
#define FFT_MAX_SIG 10 /* Maximum waveform signals per channel (per cavity) 
						* these will be processed by single thread
						*/

epicsMutexId  fftInitTaskMutex; /* Global mutex for use when initializing FFT tasks */

typedef struct FFTDataRec_ {
	const char    name[MAX_NAME_LENGTH]; /* Include space for the terminating NULL */
	char          thread_name[MAX_NAME_LENGTH];
	epicsMutexId  mutex;
	size_t        fft_max_len;	        /* Maximum input/output data length for any signal */
	size_t        len[FFT_MAX_SIG];     /* Per-signal length of last data set */
	double        tstep[FFT_MAX_SIG];   /* Per-signal time step of last data set */
	double       *data;	                /* FFT results, shared between FFT thread and device support */
	epicsMessageQueueId queue_id;
} FFTDataRec, *FFTData;

typedef struct FFTMsgRec_ {
	size_t   len;    /* Data number of elements */
	double   tstep;  /* Data time step (spacing between data points) */
	int      index;  /* Index of this signal, used as index into FFTData data array */
	double   *in_re;
	double   *in_im;
	EVENTPVT event;  /* Per-signal database scan event */
	int      debug;
} FFTMsgRec, *FFTMsg;

FFTData fftDataFind(const char *name);

int rfFFTTaskInit(char *name, size_t fft_max_len);

int fftMsgPost(epicsMessageQueueId queue_id, size_t len, double *in_re, double *in_im, double tstep, int index, EVENTPVT event, int debug);


#endif /* FFT_H */
