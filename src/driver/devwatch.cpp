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
                // had successful reply

                epicsUInt32 val(ntohl(reg->mem[offset]));

                if((val & mask) == value)
                {
                    IFDBG(1, "Match %08x & %08x != %08x",
                          (unsigned)val, (unsigned)mask, (unsigned)value);

                } else {
                    IFDBG(1, "No match %08x & %08x != %08x",
                          (unsigned)val, (unsigned)mask, (unsigned)value);

                    callbackRequestDelayed(&cb, retry);
                    done = false;
                    cb_inprogress = true;
                }

            } else IFDBG(1, "Lost attachment  %p  %u", reg, reg ? reg->state : -1);
        }catch(std::exception& e){
            ERR("error in complete() : %s\n", e.what());
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
            IFDBG(1, "No association");
        } else if(!info->reg->info.readable) {
            IFDBG(1, "Not readable");
        } else if(info->offset >= info->reg->mem.size()) {
            IFDBG(1, "Array bounds violation offset=%u not within size=%zu",
                  (unsigned)info->offset, info->reg->mem.size());
        } else if(info->reg->inprogress()) {
            IFDBG(1, "Busy");
        } else {
            if(!prec->pact) {
                IFDBG(1, "Start Watch of %s", info->reg->info.name.c_str());

                // queue read request
                info->reg->queue(false);
                // ensure our complete() is called after reply (or timeout)
                info->reg->records.push_back(info);
                // begin async
                prec->pact = 1;

            } else {
                IFDBG(1, "Complete");
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
