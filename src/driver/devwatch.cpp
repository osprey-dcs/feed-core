#include <iostream>

#include <errlog.h>
#include <callback.h>
#include <recGbl.h>
#include <alarm.h>

#include <boRecord.h>

#include "dev.h"

#include <epicsExport.h>

namespace {

#define TRY RecInfo *info = (RecInfo*)prec->dpvt; if(!info) { \
    (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); return ENODEV; } \
    Device *device=info->device; (void)device; try

#define CATCH() catch(std::exception& e) { (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); \
    errlogPrintf("%s: Error %s\n", prec->name, e.what()); info->cleanup(); return 0; }

#define IFDBG(N, FMT, ...) if(prec->tpro>(N)) errlogPrintf("%s %s : " FMT "\n", logTime(), prec->name, ##__VA_ARGS__)

struct WaitInfo : public RecInfo
{

    epicsUInt32 mask;
    epicsUInt32 value;
    double retry;
    bool cb_inprogress;
    CALLBACK cb;

    WaitInfo(dbCommon *prec, Device *dev)
        :RecInfo(prec, dev)
        ,mask(0u)
        ,value(1u)
        ,retry(1.0)
        ,cb_inprogress(false)
    {
        callbackSetCallback(&expire, &cb);
        callbackSetPriority(prec->prio, &cb);
        callbackSetUser(this, &cb);
    }

    virtual void configure(const pairs_t& pairs) {
        RecInfo::configure(pairs);
        get_pair(pairs, "mask", mask);
        get_pair(pairs, "value", value);
        get_pair(pairs, "retry", retry);
    }

    virtual void cleanup() {
        RecInfo::cleanup();
        if(cb_inprogress) {
            callbackCancelDelayed(&cb);
            cb_inprogress = false;
        }
    }

    virtual void complete()
    {
        bool done = true;
        try {
            Guard G(device->lock);
            if(reg && reg->state==DevReg::InSync) {


                epicsUInt32 val(ntohl(reg->mem[offset]));

                if((val & mask) == value)
                {
                    IFDBG(1, "Match");

                } else {
                    IFDBG(1, "No match %08x & %08x != %08x",
                          (unsigned)val, (unsigned)mask, (unsigned)value);

                    callbackRequestDelayed(&cb, retry);
                    done = false;
                }

            } else IFDBG(1, "Lost attachment  %p  %u", reg, reg ? reg->state : -1);
        }catch(std::exception& e){
            errlogPrintf("%s: error in complete() : %s\n", prec->name, e.what());
            done = true;
        }

        if(done)
            RecInfo::complete();
    }

    void done()
    {
        IFDBG(1, "retry");
        Guard G(device->lock);
        if(reg) {
            reg->queue(false); // ignore result (already Q'd is ok)
            reg->records.push_back(this);
        }
    }

    static void expire(CALLBACK *cb)
    {
        void *raw;
        callbackGetUser(raw, cb);
        WaitInfo *self = (WaitInfo*)raw;
        try {
            self->done();
        }catch(std::exception& e){
            errlogPrintf("%s : Error in timer callback : %s\n", self->prec->name, e.what());
        }
    }
};


long write_test_mask(boRecord *prec)
{
    TRY {
        Guard G(device->lock);

        if(info->reg
                && info->offset < info->reg->mem.size()
                && !info->reg->inprogress())
        {
            if(!prec->pact) {
                IFDBG(1, "Start Watch of %s", info->reg->info.name.c_str());

                info->reg->queue(false);
                info->reg->records.push_back(info);
                prec->pact = 1;

            } else {
                IFDBG(1, "Complete");
                prec->pact = 0;
            }
            return 0;

        } else {
            info->cleanup();
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            IFDBG(1, "no association %p\n", info->reg);
            return ENODEV;
        }

    }CATCH()
}

} // namespace

// register special
DSET(devBoFEEDWatchReg, bo, init_common<WaitInfo>, NULL, write_test_mask)
