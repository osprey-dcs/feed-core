
#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <stdexcept>

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
#include <menuFtype.h>
#include <stringoutRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <mbbiRecord.h>
#include <mbboRecord.h>
#include <biRecord.h>
#include <boRecord.h>
#include <aiRecord.h>
#include <aoRecord.h>
#include <aaiRecord.h>
#include <aaoRecord.h>

#include "device.h"
#include "dev.h"

#include <epicsExport.h>

#define IFDBG(N, FMT, ...) if(prec->tpro>1 || (info->device->debug&(N))) errlogPrintf("%s %s : " FMT "\n", logTime(), prec->name, ##__VA_ARGS__)

namespace {

#define TRY RecInfo *info = (RecInfo*)prec->dpvt; if(!info) { \
    (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); return ENODEV; } \
    Device *device=info->device; (void)device; try

#define CATCH() catch(std::exception& e) { (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); \
    errlogPrintf("%s: Error %s\n", prec->name, e.what()); info->cleanup(); return 0; }


long write_register_common(dbCommon *prec, const char *raw, size_t count, menuFtype ftvl)
{
    const unsigned valsize = dbValueSize(ftvl);
    TRY {
        switch(ftvl) {
        case menuFtypeCHAR:
        case menuFtypeUCHAR:
        case menuFtypeSHORT:
        case menuFtypeUSHORT:
        case menuFtypeLONG:
        case menuFtypeULONG:
        case menuFtypeDOUBLE:
            break;
        default:
            throw std::logic_error("Unsupported FTVL");
        }

        Guard G(device->lock);

        if(!info->reg) {
            IFDBG(6, "No association");
        } else if(!info->reg->info.writable) {
            IFDBG(6, "Not writable");
        } else if(info->offset >= info->reg->mem_tx.size()
                || count > info->reg->mem_tx.size() - info->offset) {
            IFDBG(6, "Array bounds violation offset=%u size=%zu not within size=%zu",
                  (unsigned)info->offset, count, info->reg->mem_tx.size());
        } else {
            if(!prec->pact) {

                const char *in = raw, *end = raw+count*valsize;

                for(size_t i=info->offset, N = info->reg->mem_tx.size();
                    i<N && in+valsize<=end; i+=info->step, in+=valsize)
                {
                    epicsUInt32 val = 0u;

                    switch(ftvl) {
                    case menuFtypeCHAR:
                    case menuFtypeUCHAR: val = *(const epicsUInt8*)in; break;
                    case menuFtypeSHORT:
                    case menuFtypeUSHORT: val = *(const epicsUInt16*)in; break;
                    case menuFtypeLONG:
                    case menuFtypeULONG: val = *(const epicsUInt32*)in; break;
                    case menuFtypeDOUBLE: val = (*(const double*)in) / info->scale; break;
                    default:
                        break;
                    }

                    info->reg->mem_tx[i] = htonl(val);
                }

                info->reg->queue(true, info->wait ? info : 0);

                if(info->wait) {
                    prec->pact = 1;
                    IFDBG(6, "begin async\n");
                }

            } else {
                // async completion

                prec->pact = 0;

                IFDBG(6, "complete async\n");
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
    return write_register_common((dbCommon*)prec, (const char*)&prec->val, 1, menuFtypeLONG);
}

long write_register_ao(aoRecord *prec)
{
    return write_register_common((dbCommon*)prec, (const char*)&prec->rval, 1, menuFtypeLONG);
}

long write_register_mbbo(mbboRecord *prec)
{
    return write_register_common((dbCommon*)prec, (const char*)&prec->rval, 1, menuFtypeULONG);
}

long write_register_aao(aaoRecord *prec)
{
    return write_register_common((dbCommon*)prec, (const char*)prec->bptr, prec->nord, (menuFtype)prec->ftvl);
}

long read_register_common(dbCommon *prec, char *raw, size_t *count, menuFtype ftvl)
{
    const unsigned valsize = dbValueSize(ftvl);
    TRY {
        size_t nreq = count ? *count : 1;

        switch(ftvl) {
        case menuFtypeCHAR:
        case menuFtypeUCHAR:
        case menuFtypeSHORT:
        case menuFtypeUSHORT:
        case menuFtypeLONG:
        case menuFtypeULONG:
        case menuFtypeDOUBLE:
            break;
        default:
            throw std::logic_error("Unsupported FTVL");
        }

        Guard G(device->lock);

        if(!info->reg) {
            IFDBG(6, "No association");
        } else if(!info->reg->info.readable) {
            IFDBG(6, "Not readable");
        } else if(info->offset >= info->reg->mem_rx.size()) {
            IFDBG(6, "Array bounds violation offset=%u not within size=%zu",
                  (unsigned)info->offset, info->reg->mem_rx.size());
        } else {
            if(prec->scan==menuScanI_O_Intr || !info->wait || prec->pact) {
                // I/O Intr scan, use cached, or async completion

                // mask for sign extension
                epicsUInt32 signmask = 0;

                if(info->reg->info.sign==JRegister::Signed) {
                    // mask of sign bit and higher.
                    signmask = 0xffffffff << (info->reg->info.data_width-1);
                }

                char *out = raw, *end = raw+nreq*valsize;
                for(size_t i=info->offset, N = info->reg->mem_rx.size();
                    i<N && out+valsize<=end; i+=info->step, out+=valsize)
                {
                    epicsUInt32 val = ntohl(info->reg->mem_rx[i]);
                    if(val & signmask)
                        val |= signmask;

                    switch(ftvl) {
                    case menuFtypeCHAR:
                    case menuFtypeUCHAR: *(epicsUInt8*)out = val; break;
                    case menuFtypeSHORT:
                    case menuFtypeUSHORT: *(epicsUInt16*)out = val; break;
                    case menuFtypeLONG:
                    case menuFtypeULONG: *(epicsUInt32*)out = val; break;
                    case menuFtypeDOUBLE:
                        if(signmask)
                            *(double*)out = epicsInt32(val) * info->scale;
                        else
                            *(double*)out = val * info->scale;
                        break;
                    default:
                        break;
                    }
                }

                assert((ssize_t)nreq >= (out-raw)/valsize);
                nreq = (out-raw)/valsize;

                prec->pact = 0;
                if(count)
                    *count = nreq;

                if(prec->tse==epicsTimeEventDeviceTime) {
                    prec->time = info->reg->rx;
                }

                (void)recGblSetSevr(prec, info->reg->stat, info->reg->sevr);
                IFDBG(6, "Copy in %zu of %zu words.  sevr=%u offset=%u step=%u valsize=%u\n",
                      nreq, info->reg->mem_rx.size(),
                      info->reg->sevr, (unsigned)info->offset, (unsigned)info->step, valsize);

            } else {
                info->reg->queue(false, info->wait ? info : 0);

                prec->pact = 1;
                if(count)
                    *count = 0;

                IFDBG(6, "begin async\n");
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
    return read_register_common((dbCommon*)prec, (char*)&prec->val, 0, menuFtypeLONG);
}

long read_register_ai(aiRecord *prec)
{
    return read_register_common((dbCommon*)prec, (char*)&prec->rval, 0, menuFtypeLONG);
}

long read_register_mbbi(mbbiRecord *prec)
{
    return read_register_common((dbCommon*)prec, (char*)&prec->rval, 0, menuFtypeULONG);
}

long read_register_aai(aaiRecord *prec)
{
    size_t cnt = prec->nelm;
    long ret = read_register_common((dbCommon*)prec, (char*)prec->bptr, &cnt, (menuFtype)prec->ftvl);
    prec->nord = cnt;
    return ret;
}

} // namespace

// register writes
DSET(devLoFEEDWriteReg, longout, init_common<RecInfo>::fn, NULL, write_register_lo)
DSET(devAoFEEDWriteReg, ao, init_common<RecInfo>::fn, NULL, write_register_ao)
DSET(devMbboFEEDWriteReg, mbbo, init_common<RecInfo>::fn, NULL, write_register_mbbo)
DSET(devAaoFEEDWriteReg, aao, init_common<RecInfo>::fn, NULL, write_register_aao)

// register reads
DSET(devLiFEEDWriteReg, longin, init_common<RecInfo>::fn, get_reg_changed_intr, read_register_li)
DSET(devAiFEEDWriteReg, ai, init_common<RecInfo>::fn, get_reg_changed_intr, read_register_ai)
DSET(devMbbiFEEDWriteReg, mbbi, init_common<RecInfo>::fn, get_reg_changed_intr, read_register_mbbi)
DSET(devAaiFEEDWriteReg, aai, init_common<RecInfo>::fn, get_reg_changed_intr, read_register_aai)
