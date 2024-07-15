
#include <stdexcept>
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
#include <dbEvent.h>
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

template<typename Rec>
struct RecRegInfo final : public RecInfo
{
    RecRegInfo(dbCommon *prec, Device *device)
        :RecInfo(prec, device)
    {}
    virtual ~RecRegInfo() {}

    virtual void connected() override;
};

#define TRY RecInfo *info = static_cast<RecInfo*>(prec->dpvt); if(!info) { \
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

                if(count) {
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
                }

                if(info->commit) {
                    info->reg->queue(true, info->wait ? info : 0);

                    if(info->wait && info->device->active()) {
                        prec->pact = 1;
                        IFDBG(6, "begin async\n");
                    }
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

long write_register_bo(boRecord *prec)
{
    return write_register_common((dbCommon*)prec, (const char*)&prec->rval, 1, menuFtypeULONG);
}

long write_register_mbbo(mbboRecord *prec)
{
    return write_register_common((dbCommon*)prec, (const char*)&prec->rval, 1, menuFtypeULONG);
}

long write_register_aao(aaoRecord *prec)
{
    return write_register_common((dbCommon*)prec, (const char*)prec->bptr, prec->nord, (menuFtype)prec->ftvl);
}

long flush_register_bo(boRecord *prec)
{
    return write_register_common((dbCommon*)prec, NULL, 0, menuFtypeULONG);
}

long read_register_common(dbCommon *prec, char *raw, size_t *count, menuFtype ftvl)
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

        } else {
            DevReg::mem_t& mem = info->rbv ? info->reg->mem_tx : info->reg->mem_rx;

            size_t nreq = 1, size = mem.size();

            if ( count ) {
                nreq = *count;
                /* Optionally use fewer elements of register */
                if ( (info->size > 0) && (info->size < size) ) {
                    size = info->size;
                }
            }

            if(!info->rbv && !info->reg->info.readable) {
                IFDBG(6, "Not readable");
            } else if(info->rbv && !info->reg->info.writable) {
                    IFDBG(6, "Not writable");
            } else if(info->offset >= mem.size()) {
                IFDBG(6, "Array bounds violation offset=%u not within size=%zu",
                      (unsigned)info->offset, mem.size());
            } else {
                if(prec->scan==menuScanI_O_Intr || !info->wait || prec->pact || !info->device->active()) {
                    // I/O Intr scan, use cached, async completion, or no comm.

                    // mask for sign extension
                    epicsUInt32 signmask = 0;

                    if(info->reg->info.sign==JRegister::Signed) {
                        // mask of sign bit and higher.
                        signmask = 0xffffffff << (info->reg->info.data_width-1);
                    }

                    char *out = raw, *end = raw+nreq*valsize;
                    for(size_t i=info->offset, N = size;
                        i<N && out+valsize<=end; i+=info->step, out+=valsize)
                    {
                        epicsUInt32 val = ntohl(mem[i]);
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
                    IFDBG(6, "Copy in %zu of %zu words.  sevr=%u offset=%u step=%u valsize=%u size=%zu\n",
                          nreq, mem.size(),
                          info->reg->sevr, (unsigned)info->offset, (unsigned)info->step, valsize, size);

                } else {
                    info->reg->queue(false, info->wait ? info : 0);

                    prec->pact = 1;
                    if(count)
                        *count = 0;

                    IFDBG(6, "begin async\n");
                }
                return 0;

            }
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

long read_register_bi(biRecord *prec)
{
    long ret = read_register_common((dbCommon*)prec, (char*)&prec->rval, 0, menuFtypeULONG);
    if(prec->mask)
        prec->rval &= prec->mask;
    return ret;
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

template<typename fld_t>
void maybePost(dbCommon *prec, fld_t *fld, epicsInt64 value)
{
    fld_t fval = value;
    if(fval!=*fld) {
        *fld = value;
        db_post_events(prec, fld, DBE_VALUE);
        db_post_events(prec, NULL, DBE_PROPERTY);
    }
}

void* getVAL(dbCommon *prec)
{
    DBENTRY ent;
    dbInitEntryFromRecord(prec, &ent);
    void *pfield = ent.precordType->pvalFldDes->offset + (char*)prec;
    dbFinishEntry(&ent);
    return pfield;
}

void persistSettings(RecInfo *info)
{
    dbCommon *prec = info->prec;
    if(prec->udf)
        return; // do not persist undefined/invalid

    IFDBG(6, "Persisting");

    const dset6<dbCommon> * dset = reinterpret_cast<dset6<dbCommon>*>(prec->dset);

    /* Partially "process" to update TX cache from VAL,
     * but do not trigger any links.
     */
    bool save = info->commit;
    info->commit = false;
    dset->readwrite(prec);
    info->commit = save;

    unsigned short monitor_mask = recGblResetAlarms(prec);
    if (monitor_mask)
        db_post_events(prec, getVAL(prec), monitor_mask);
}

template<> void RecRegInfo<longoutRecord>::connected() {
    ScanLock G(prec);
    persistSettings(this);
    if(meta) {
        longoutRecord *prec = (longoutRecord*)this->prec;
        assert(reg);
        maybePost(this->prec, &prec->drvl, reg->info.min());
        maybePost(this->prec, &prec->lopr, reg->info.min());
        maybePost(this->prec, &prec->drvh, reg->info.max());
        maybePost(this->prec, &prec->hopr, reg->info.max());
    }
}

template<> void RecRegInfo<aoRecord>::connected() { ScanLock G(prec); persistSettings(this);}
template<> void RecRegInfo<boRecord>::connected() { ScanLock G(prec); persistSettings(this); }
template<> void RecRegInfo<mbboRecord>::connected() { ScanLock G(prec); persistSettings(this); }
template<> void RecRegInfo<aaoRecord>::connected() { ScanLock G(prec); persistSettings(this); }

template<> void RecRegInfo<longinRecord>::connected() {
    if(meta) {
        ScanLock G(prec);
        longinRecord *prec = (longinRecord*)this->prec;
        assert(reg);
        maybePost(this->prec, &prec->lopr, reg->info.min());
        maybePost(this->prec, &prec->hopr, reg->info.max());
    }
}

template<> void RecRegInfo<aiRecord>::connected() {}
template<> void RecRegInfo<biRecord>::connected() {}
template<> void RecRegInfo<mbbiRecord>::connected() {}
template<> void RecRegInfo<aaiRecord>::connected() {}

} // namespace

// register writes
DSET(devLoFEEDWriteReg, longout, init_common<RecRegInfo<longoutRecord> >::fn, NULL, write_register_lo)
DSET(devAoFEEDWriteReg, ao, init_common<RecRegInfo<aoRecord> >::fn, NULL, write_register_ao)
DSET(devBoFEEDWriteReg, bo, init_common<RecRegInfo<boRecord> >::fn, NULL, write_register_bo)
DSET(devMbboFEEDWriteReg, mbbo, init_common<RecRegInfo<mbboRecord> >::fn, NULL, write_register_mbbo)
DSET(devAaoFEEDWriteReg, aao, init_common<RecRegInfo<aaoRecord> >::fn, NULL, write_register_aao)
DSET(devBoFEEDFlushReg, bo, init_common<RecRegInfo<boRecord> >::fn, NULL, flush_register_bo)

// register reads
DSET(devLiFEEDWriteReg, longin, init_common<RecRegInfo<longinRecord> >::fn, get_reg_changed_intr, read_register_li)
DSET(devAiFEEDWriteReg, ai, init_common<RecRegInfo<aiRecord> >::fn, get_reg_changed_intr, read_register_ai)
DSET(devBiFEEDWriteReg, bi, init_common<RecRegInfo<biRecord> >::fn, get_reg_changed_intr, read_register_bi)
DSET(devMbbiFEEDWriteReg, mbbi, init_common<RecRegInfo<mbbiRecord> >::fn, get_reg_changed_intr, read_register_mbbi)
DSET(devAaiFEEDWriteReg, aai, init_common<RecRegInfo<aaiRecord> >::fn, get_reg_changed_intr, read_register_aai)
