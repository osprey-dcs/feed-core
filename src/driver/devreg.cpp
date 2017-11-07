
#include <iostream>
#include <memory>
#include <string>
#include <map>

#include <stdio.h>

#include <epicsStdlib.h>
#include <alarm.h>
#include <errlog.h>
#include <recGbl.h>
#include <devSup.h>
#include <drvSup.h>
#include <link.h>
#include <osiSock.h>

#include <dbAccess.h>
#include <recSup.h>
#include <dbStaticLib.h>
#include <callback.h>
#include <menuScan.h>
#include <dbCommon.h>
#include <stringoutRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <mbbiRecord.h>
#include <biRecord.h>
#include <boRecord.h>
#include <aiRecord.h>
#include <aoRecord.h>
#include <aaiRecord.h>
#include <aaoRecord.h>

#include "device.h"
#include "dev.h"

#include <epicsExport.h>

#define IFDBG(N, FMT, ...) if(prec->tpro>(N)) errlogPrintf("%s %s : " FMT "\n", logTime(), prec->name, ##__VA_ARGS__)

namespace {

#define TRY RecInfo *info = (RecInfo*)prec->dpvt; if(!info) { \
    (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); return ENODEV; } \
    Device *device=info->device; (void)device; try

#define CATCH() catch(std::exception& e) { (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); \
    errlogPrintf("%s: Error %s\n", prec->name, e.what()); info->cleanup(); return 0; }


long write_register_common(dbCommon *prec, const char *raw, size_t count, unsigned valsize)
{
    TRY {
        Guard G(device->lock);

        if(!info->reg) {
            IFDBG(1, "No association");
        } else if(!info->reg->info.writable) {
            IFDBG(1, "Not writable");
        } else if(info->offset >= info->reg->mem.size()
                || count > info->reg->mem.size() - info->offset) {
            IFDBG(1, "Array bounds violation offset=%u size=%zu not within size=%zu",
                  (unsigned)info->offset, count, info->reg->mem.size());
        } else if(info->reg->inprogress()) {
            IFDBG(1, "Busy");
        } else {
            if(!prec->pact) {

                const char *out = raw, *end = raw+count*valsize;

                for(size_t i=info->offset, N = info->reg->mem.size();
                    i<N && out+valsize<=end; i+=info->step, out+=valsize)
                {
                    epicsUInt32 val;

                    switch(valsize) {
                    case 1: val = *(const epicsUInt8*)raw; break;
                    case 2: val = *(const epicsUInt16*)raw; break;
                    case 4: val = *(const epicsUInt32*)raw; break;
                    }

                    info->reg->mem[i] = htonl(val);
                }

                if(!info->reg->queue(true)) {
                    (void)recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
                    return ENODEV;
                }

                if(info->wait) {
                    info->reg->records.push_back(info);
                    prec->pact = 1;
                    IFDBG(1, "begin async\n");
                }

            } else {
                // async completion

                prec->pact = 0;

                IFDBG(1, "complete async\n");
            }
            return 0;
        }

        info->cleanup();
        (void)recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
        return ENODEV;

    }CATCH()
}

long write_register_lo(longoutRecord *prec)
{
    return write_register_common((dbCommon*)prec, (const char*)&prec->val, 1, sizeof(prec->val));
}

long write_register_ao(aoRecord *prec)
{
    return write_register_common((dbCommon*)prec, (const char*)&prec->rval, 1, sizeof(prec->val));
}

long write_register_aao(aaoRecord *prec)
{
    return write_register_common((dbCommon*)prec, (const char*)prec->bptr, prec->nord, dbValueSize(prec->ftvl));
}

long read_register_common(dbCommon *prec, char *raw, size_t *count, unsigned valsize)
{
    TRY {
        size_t nreq = count ? *count : 1;

        Guard G(device->lock);

        if(!info->reg) {
            IFDBG(1, "No association");
        } else if(!info->reg->info.readable) {
            IFDBG(1, "Not readable");
        } else if(info->offset >= info->reg->mem.size()) {
            IFDBG(1, "Array bounds violation offset=%u not within size=%zu",
                  (unsigned)info->offset, info->reg->mem.size());
        } else if(info->reg->inprogress()) {
            IFDBG(1, "Busy");
        } else {
            if(prec->scan==menuScanI_O_Intr || !info->wait || prec->pact) {
                // I/O Intr scan, use current, or async completion

                // mask for sign extension
                epicsUInt32 signmask = 0;

                if(info->reg->info.sign==JRegister::Signed) {
                    // mask of sign bit and higher.
                    signmask = 0xffffffff << (info->reg->info.data_width-1);
                }

                char *out = raw, *end = raw+nreq*valsize;

                for(size_t i=info->offset, N = info->reg->mem.size();
                    i<N && out+valsize<=end; i+=info->step, out+=valsize)
                {
                    epicsUInt32 val = ntohl(info->reg->mem[i]);
                    if(val & signmask)
                        val |= signmask;

                    switch(valsize) {
                    case 1: *(epicsUInt8*)out = val; break;
                    case 2: *(epicsUInt16*)out = val; break;
                    case 4: *(epicsUInt32*)out = val; break;
                    }
                }

                nreq = (out-raw)/valsize;

                prec->pact = 0;
                if(count)
                    *count = nreq;

                if(prec->tse==epicsTimeEventDeviceTime) {
                    prec->time = info->reg->rx;
                }

                (void)recGblSetSevr(prec, info->reg->stat, info->reg->sevr);
                IFDBG(1, "Copy in %zu words.  sevr=%u offset=%u step=%u\n",
                                 nreq, info->reg->sevr, (unsigned)info->offset, (unsigned)info->step);

            } else {
                if(!info->reg->queue(false)) {
                    (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
                    IFDBG(1, "failed to queue\n");
                    return ENODEV;
                } else {
                    info->reg->records.push_back(info);
                }

                prec->pact = 1;
                if(count)
                    *count = 0;

                IFDBG(1, "begin async\n");
            }
            return 0;

        }

        info->cleanup();
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        return ENODEV;

    }CATCH()
}

long read_register_li(longinRecord *prec)
{
    return read_register_common((dbCommon*)prec, (char*)&prec->val, 0, sizeof(prec->val));
}

long read_register_ai(aiRecord *prec)
{
    return read_register_common((dbCommon*)prec, (char*)&prec->rval, 0, sizeof(prec->rval));
}

long read_register_aai(aaiRecord *prec)
{
    size_t cnt = prec->nelm * dbValueSize(prec->ftvl) /4u;
    long ret = read_register_common((dbCommon*)prec, (char*)prec->bptr, &cnt, dbValueSize(prec->ftvl));
    prec->nord = cnt;
    return ret;
}

} // namespace

// register writes
DSET(devLoFEEDWriteReg, longout, init_common<RecInfo>::fn, NULL, write_register_lo)
DSET(devAoFEEDWriteReg, ao, init_common<RecInfo>::fn, NULL, write_register_ao)
DSET(devAaoFEEDWriteReg, aao, init_common<RecInfo>::fn, NULL, write_register_aao)

// register reads
DSET(devLiFEEDWriteReg, longin, init_common<RecInfo>::fn, get_reg_changed_intr, read_register_li)
DSET(devAiFEEDWriteReg, ai, init_common<RecInfo>::fn, get_reg_changed_intr, read_register_ai)
DSET(devAaiFEEDWriteReg, aai, init_common<RecInfo>::fn, get_reg_changed_intr, read_register_aai)
