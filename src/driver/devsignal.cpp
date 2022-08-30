#include <stdexcept>
#include <memory>

#include <errno.h>

#include <alarm.h>
#include <errlog.h>
#include <recGbl.h>
#include <devSup.h>

#include <longoutRecord.h>
#include <aoRecord.h>

#include "device.h"
#include "dev.h"

#include <epicsExport.h>

#define IFDBG(N, FMT, ...) if(prec->tpro>(N)) errlogPrintf("%s %s : signal %s " FMT "\n", logTime(), prec->name, info->signame.c_str(), ##__VA_ARGS__)

namespace {

struct SigInfo {
    dbCommon * const prec;
    std::string signame;
    RecInfo *siginfo;
    bool warned;

    explicit SigInfo(dbCommon* prec) :prec(prec), siginfo(0), warned(false) {}

    bool lookup()
    {
        if(siginfo)
            return true;

        RecInfo::signals_t::iterator it(RecInfo::signals.find(signame));
        if(it==RecInfo::signals.end()) {
            if(!warned) {
                errlogPrintf("%s : no such signal name %s\n", prec->name, signame.c_str());
                warned = true;
            }
            return false;
        } else {
            siginfo = it->second;
            return true;
        }
    }

    void cleanup() {}
};

#define TRY SigInfo *info = static_cast<SigInfo*>(prec->dpvt); if(!info || !info->lookup()) { \
    (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); return ENODEV; } \
    Device *device=info->siginfo->device; (void)device; try

#define CATCH() catch(std::exception& e) { (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); \
    errlogPrintf("%s: Error %s\n", prec->name, e.what()); info->cleanup(); return 0; }

static
long init_signal(dbCommon *prec)
{
    try {
        DBLINK *plink = getDevLnk(prec);
        assert(plink->type==INST_IO);
        const char *lstr = plink->value.instio.string;

        pairs_t pairs;
        split_pairs(lstr, pairs);
        if(prec->tpro>2) {
            for(pairs_t::const_iterator it(pairs.begin()), end(pairs.end()); it!=end; ++it)
            {
                printf("%s : '%s'='%s'\n", prec->name, it->first.c_str(), it->second.c_str());
            }
        }

        feed::auto_ptr<SigInfo> info(new SigInfo(prec));

        if(!get_pair(pairs, "signal", info->signame))
            throw std::runtime_error("Omitted required key signal=");

        prec->dpvt = info.release();
        return 0;
    } catch(std::exception& e){
        fprintf(stderr, "%s: Error %s\n", prec->name, e.what());
        return -EIO;
    }
}

static long init_signal2(dbCommon *prec)
{
    long ret = init_signal(prec);
    if(ret==0)
        ret = 2;
    return ret;
}

static
long write_signal_offset(longoutRecord *prec)
{
    TRY {
        if(prec->val<0) {
            (void)recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
            return EINVAL;
        }

        Guard G(device->lock);

        info->siginfo->offset = prec->val;
        IFDBG(1, "set offset=%u", (unsigned)info->siginfo->offset);

        return 0;
    }CATCH()
}

static
long write_signal_step(longoutRecord *prec)
{
    TRY {
        if(prec->val<=0) {
            (void)recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
            return EINVAL;
        }

        Guard G(device->lock);

        info->siginfo->step = prec->val;
        IFDBG(1, "set step=%u", (unsigned)info->siginfo->step);

        return 0;
    }CATCH()
}

static
long write_signal_slope(aoRecord *prec)
{
    TRY {
        if(prec->val<=0) {
            (void)recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
            return EINVAL;
        }

        Guard G(device->lock);

        info->siginfo->scale = prec->val;
        IFDBG(1, "set step=%u", (unsigned)info->siginfo->step);

        return 0;
    }CATCH()
}

// Optional size limit for array registers
// If set less than register size, will limit the range
// of elements copied into record. See devreg.cpp
static
long write_signal_size(longoutRecord *prec)
{
    TRY {
        if(prec->val<=0) {
            (void)recGblSetSevr(prec, WRITE_ALARM, INVALID_ALARM);
            return EINVAL;
        }

        Guard G(device->lock);

        info->siginfo->size = prec->val;
        IFDBG(1, "set size=%u", (unsigned)info->siginfo->size);

        return 0;
    }CATCH()
}

} // namespace

DSET(devLoFEEDSigOffset, longout, init_signal, NULL, write_signal_offset)
DSET(devLoFEEDSigStep, longout, init_signal, NULL, write_signal_step)
DSET(devAoFEEDSigScale, ao, init_signal2, NULL, write_signal_slope)
DSET(devLoFEEDSigSize, longout, init_signal2, NULL, write_signal_size)
