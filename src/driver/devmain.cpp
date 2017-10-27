
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

long get_dev_changed_intr(int dir, dbCommon *prec, IOSCANPVT *scan)
{
    RecInfo *info = (RecInfo*)prec->dpvt;
    if(info)
        *scan = info->device->current_changed;
    return scan ? 0 : ENODEV;
}

long get_reg_changed_intr(int dir, dbCommon *prec, IOSCANPVT *scan)
{
    RecInfo *info = (RecInfo*)prec->dpvt;
    if(info)
        *scan = info->changed;
    return 0;
}

namespace {

#define TRY RecInfo *info = (RecInfo*)prec->dpvt; if(!info) { \
    (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); return ENODEV; } \
    Device *device=info->device; (void)device; try

#define CATCH() catch(std::exception& e) { (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); \
    errlogPrintf("%s: Error %s\n", prec->name, e.what()); info->cleanup(); return 0; }

long write_debug(longoutRecord *prec)
{
    TRY {
        Guard G(device->lock);
        device->debug = prec->val;
        return 0;
    }CATCH()
}

long write_address(stringoutRecord *prec)
{
    TRY {
        osiSockAddr addr;

        if(prec->val[0] && aToIPAddr(prec->val, 50006, &addr.ia)) {
            (void)recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
            return EINVAL;
        }

        Guard G(device->lock);

        device->request_reset();
        device->peer_name = prec->val;
        device->peer_addr = addr;
        device->poke_runner();
        return 0;
    }CATCH()
}


long read_dev_state(mbbiRecord *prec)
{
    TRY {
        Guard G(device->lock);
        prec->rval = (int)device->current;
        return 0;
    }CATCH()
}

long read_reg_state(mbbiRecord *prec)
{
    TRY {
        Guard G(device->lock);
        if(info->reg)
            prec->rval = 1+(int)info->reg->state;
        else
            prec->rval = 0;
        return 0;
    }CATCH()
}


long read_counter(longinRecord *prec)
{
    TRY {
        Guard G(device->lock);

        switch(info->offset) {
        case 0: prec->val = device->cnt_sent; break;
        case 1: prec->val = device->cnt_recv; break;
        case 2: prec->val = device->cnt_ignore; break;
        case 3: prec->val = device->cnt_timo; break;
        case 4: prec->val = device->cnt_err; break;
        case 5: prec->val = device->send_seq; break;
        default:
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        }
        return 0;
    }CATCH()
}

long read_error(aaiRecord *prec)
{
    TRY {
        if(prec->nelm<2)
            throw std::runtime_error("Need NELM>=2");
        Guard G(device->lock);

        char *buf = (char*)prec->bptr;
        size_t N = std::min(size_t(prec->nelm), device->last_message.size()+1);

        std::copy(device->last_message.begin(),
                  device->last_message.begin()+N,
                  buf);

        buf[N-1] = '\0';
        prec->nord = N;

        return 0;
    }CATCH()
}

long write_commit(boRecord *prec)
{
    TRY {
        // no locking necessary
        device->poke_runner();
        return 0;
    }CATCH()
}

long write_register_common(dbCommon *prec, const epicsInt32 *raw, size_t count, unsigned valsize)
{
    const epicsUInt32 *value = (const epicsUInt32 *)raw;
    TRY {
        Guard G(device->lock);

        if(info->reg
                && info->offset < info->reg->mem.size()
                && info->offset+count <= info->reg->mem.size()
                && !info->reg->inprogress())
        {
            if(!prec->pact) {
                switch(valsize) {
                case 2: {
                    epicsUInt16 *out = (epicsUInt16*)&info->reg->mem[info->offset],
                                *in  = (epicsUInt16*)value;
                    for(size_t i=0; i<count; i++)
                        out[i] = htons(in[i]);
                }
                    break;
                case 4: {
                    epicsUInt32 *buf = &info->reg->mem[info->offset];
                    for(size_t i=0; i<count; i++)
                        buf[i] = htonl(value[i]);
                }
                    break;
                default:
                    std::copy(value,
                              value+count,
                              info->reg->mem.begin()+info->offset);
                    break;
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

        } else {
            prec->pact = 0;
            IFDBG(1, "no association %p %u\n", info->reg, info->reg ? info->reg->state : -1);
            (void)recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
            return ENODEV;
        }

    }CATCH()
}

long write_register_lo(longoutRecord *prec)
{
    return write_register_common((dbCommon*)prec, &prec->val, 1, sizeof(prec->val));
}

long write_register_ao(aoRecord *prec)
{
    return write_register_common((dbCommon*)prec, &prec->rval, 1, sizeof(prec->val));
}

long write_register_aao(aaoRecord *prec)
{
    return write_register_common((dbCommon*)prec, (epicsInt32*)prec->bptr, prec->nord, dbValueSize(prec->ftvl));
}

long read_register_common(dbCommon *prec, epicsInt32 *raw, size_t *count, unsigned valsize)
{
    epicsUInt32 *value = (epicsUInt32 *)raw;
    TRY {
        size_t nreq = count ? *count : 1;

        Guard G(device->lock);

        if(info->reg
                && info->offset < info->reg->mem.size()
                && !info->reg->inprogress())
        {
            if(nreq > info->reg->mem.size() - info->offset)
                nreq = info->reg->mem.size() - info->offset;

            if(prec->scan==menuScanI_O_Intr || !info->wait || prec->pact) {
                // I/O Intr scan, use current, or async completion

                switch(valsize) {
                case 2: {
                    epicsUInt16 *in= (epicsUInt16*)&info->reg->mem[info->offset],
                                *out=(epicsUInt16*)value;
                    for(size_t i=0; i<nreq*2; i++)
                        out[i] = ntohs(in[i]);
                }
                    break;
                case 4: {
                    epicsUInt32 *ptr= &info->reg->mem[info->offset];
                    for(size_t i=0; i<nreq; i++)
                        value[i] = ntohl(ptr[i]);
                }
                    break;
                // TODO: 8
                default:
                    std::copy(info->reg->mem.begin() + info->offset,
                              info->reg->mem.begin() + info->offset + nreq,
                              value);
                    break;
                }

                prec->pact = 0;
                if(count)
                    *count = nreq;

                (void)recGblSetSevr(prec, info->reg->stat, info->reg->sevr);
                IFDBG(1, "Copy in %zu words.  sevr=%u\n",
                                 nreq, info->reg->sevr);

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

        } else {
            prec->pact = 0;
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            IFDBG(1, "no association %p\n", info->reg);
            return ENODEV;
        }

    }CATCH()
}

long read_register_li(longinRecord *prec)
{
    return read_register_common((dbCommon*)prec, &prec->val, 0, sizeof(prec->val));
}

long read_register_ai(aiRecord *prec)
{
    return read_register_common((dbCommon*)prec, &prec->rval, 0, sizeof(prec->rval));
}

long read_register_aai(aaiRecord *prec)
{
    size_t cnt = prec->nelm * dbValueSize(prec->ftvl) /4u;
    long ret = read_register_common((dbCommon*)prec, (epicsInt32*)prec->bptr, &cnt, dbValueSize(prec->ftvl));
    prec->nord = cnt;
    return ret;
}

} // namespace

// device-wide settings
DSET(devSoFEEDDebug, longout, init_common<RecInfo>, NULL, write_debug)
DSET(devSoFEEDAddress, stringout, init_common<RecInfo>, NULL, write_address)
DSET(devBoFEEDCommit, bo, init_common<RecInfo>, NULL, write_commit)

// device-wide status
DSET(devMbbiFEEDDevState, mbbi, init_common<RecInfo>, get_dev_changed_intr, read_dev_state)
DSET(devLiFEEDCounter, longin, init_common<RecInfo>, get_dev_changed_intr, read_counter)
DSET(devAaiFEEDError, aai, init_common<RecInfo>, get_dev_changed_intr, read_error)

// register status
DSET(devMbbiFEEDRegState, mbbi, init_common<RecInfo>, get_reg_changed_intr, read_reg_state)

// register writes
DSET(devLoFEEDWriteReg, longout, init_common<RecInfo>, NULL, write_register_lo)
DSET(devAoFEEDWriteReg, ao, init_common<RecInfo>, NULL, write_register_ao)
DSET(devAaoFEEDWriteReg, aao, init_common<RecInfo>, NULL, write_register_aao)

// register reads
DSET(devLiFEEDWriteReg, longin, init_common<RecInfo>, get_reg_changed_intr, read_register_li)
DSET(devAiFEEDWriteReg, ai, init_common<RecInfo>, get_reg_changed_intr, read_register_ai)
DSET(devAaiFEEDWriteReg, aai, init_common<RecInfo>, get_reg_changed_intr, read_register_aai)
