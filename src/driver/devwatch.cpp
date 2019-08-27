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

#define IFDBG(N, FMT, ...) if(prec->tpro>1 || (info->device->debug&(N))) errlogPrintf("%s %s : " FMT "\n", logTime(), prec->name, ##__VA_ARGS__)
#define ERR(FMT, ...) errlogPrintf("%s %s : " FMT "\n", logTime(), prec->name, ##__VA_ARGS__)

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
        memset(&cb, 0, sizeof(cb));
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
        WaitInfo * const info = this;
        bool done = true;
        try {
            Guard G(device->lock);
            if(reg && reg->state==DevReg::InSync) {
                // had successful reply

                epicsUInt32 val(ntohl(reg->mem_rx[offset]));

                if((val & mask) == value)
                {
                    IFDBG(6, "Match %08x & %08x == %08x",
                          (unsigned)val, (unsigned)mask, (unsigned)value);

                } else {
                    IFDBG(6, "No match %08x & %08x != %08x",
                          (unsigned)val, (unsigned)mask, (unsigned)value);

                    callbackRequestDelayed(&cb, retry);
                    done = false;
                    cb_inprogress = true;
                }

            } else IFDBG(6, "Lost attachment  %p  %u", reg, reg ? reg->state : -1);
        }catch(std::exception& e){
            ERR("error in complete() : %s\n", e.what());
            done = true;
        }

        if(done)
            RecInfo::complete();
    }

    void done()
    {
        WaitInfo * const info = this;
        IFDBG(6, "retry");
        Guard G(device->lock);
        if(reg) {
            reg->queue(false, this);
        }
    }

    static void expire(CALLBACK *cb)
    {
        void *raw;
        callbackGetUser(raw, cb);
        WaitInfo *self = (WaitInfo*)raw;
        dbCommon *prec = self->prec;
        try {
            self->done();
        }catch(std::exception& e){
            ERR("Error in timer callback : %s\n", e.what());
        }
    }
};


long write_test_mask(boRecord *prec)
{
    TRY {
        Guard G(device->lock);

        if(!info->reg) {
            IFDBG(6, "No association");
        } else if(!info->reg->info.readable) {
            IFDBG(6, "Not readable");
        } else if(info->offset >= info->reg->mem_rx.size()) {
            IFDBG(6, "Array bounds violation offset=%u not within size=%zu",
                  (unsigned)info->offset, info->reg->mem_rx.size());
        } else {
            if(!prec->pact) {
                IFDBG(6, "Start Watch of %s", info->reg->info.name.c_str());

                // queue read request
                // ensure our complete() is called after reply (or timeout)
                info->reg->queue(false, info);
                // begin async
                prec->pact = 1;

            } else {
                IFDBG(6, "Complete");
                prec->pact = 0;
            }
            return 0;

        }

        info->cleanup();
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        return ENODEV;

    }CATCH()
}

} // namespace

// register special
DSET(devBoFEEDWatchReg, bo, init_common<WaitInfo>::fn, NULL, write_test_mask)
