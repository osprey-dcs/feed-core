#include <stdio.h>
#include <stdlib.h>

#include <epicsExport.h>
#include <registry.h>
#include <registryFunction.h>
#include <aSubRecord.h>

static long 
noop(aSubRecord* prec)
{
    return 1;
}

static registryFunctionRef fft_asub_seq[] = {
    {"FFTSENDINIT", (REGISTRYFUNCTION) &noop},
    {"FFTCALCINIT", (REGISTRYFUNCTION) &noop},
    {"FFTSEND",     (REGISTRYFUNCTION) &noop},
    {"FFTCALC",     (REGISTRYFUNCTION) &noop},
};

static
void asubFFTRegister(void) {
    registryFunctionRefAdd(fft_asub_seq, NELEMENTS(fft_asub_seq));
}

#include <epicsExport.h>

epicsExportRegistrar(asubFFTRegister);

static void
fftRegistrar() {
}

epicsExportRegistrar(fftRegistrar);
