#include <stdexcept>
#include <iostream>
#include <memory>
#include <string>
#include <map>

#include <stdio.h>

#include <epicsMath.h>
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
#define ERR(FMT, ...) errlogPrintf("%s %s : " FMT "\n", logTime(), prec->name, ##__VA_ARGS__)

// UDP port numbers:
//   50006 copper interface
//   803 fiber interface
// Variable feedUDPPortNum can be used to change
//   default port number for all devices.
// User can still override for individual devices
//   by appending :port to end of IP/node.
unsigned short feedUDPPortNum = 50006;

long get_on_connect_intr(int dir, dbCommon *prec, IOSCANPVT *scan)
{
    RecInfo *info = static_cast<RecInfo*>(prec->dpvt);
    if(info)
        *scan = info->device->on_connect;
    return scan ? 0 : ENODEV;
}

long get_dev_changed_intr(int dir, dbCommon *prec, IOSCANPVT *scan)
{
    RecInfo *info = static_cast<RecInfo*>(prec->dpvt);
    if(info)
        *scan = info->device->current_changed;
    return scan ? 0 : ENODEV;
}

long get_reg_changed_intr(int dir, dbCommon *prec, IOSCANPVT *scan)
{
    RecInfo *info = static_cast<RecInfo*>(prec->dpvt);
    if(info)
        *scan = info->changed;
    return 0;
}

namespace {

#define TRY RecInfo *info = static_cast<RecInfo*>(prec->dpvt); if(!info) { \
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

        if(prec->val[0] && aToIPAddr(prec->val, feedUDPPortNum, &addr.ia)) {
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

long force_error(stringoutRecord *prec)
{
    TRY {
        Guard G(device->lock);

        device->last_message = prec->val;

        device->error_requested = true;

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

// something to go with get_on_connect_intr()
long read_inc(longinRecord *prec)
{
    prec->val++;
    return 0;
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

long read_jsoninfo(aaiRecord *prec)
{
    TRY {
        if(prec->ftvl!=menuFtypeCHAR) {
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            return -1;
        }
        char *buf = (char*)prec->bptr;

        Guard G(device->lock);
        const std::string *str;

        switch(info->offset) {
        case 0: str = &device->description; break;
        case 1: str = &device->jsonhash; break;
        case 2: str = &device->codehash; break;
        default:
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            return -1;
        }

        // len includes trailing nil
        const size_t len  = std::min(str->size()+1, size_t(prec->nelm));
        if(len==0) {
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
            return -1;
        }

        memcpy(buf, str->c_str(), len);
        buf[len-1] = '\0';

        prec->nord = len;
        return 0;
    } CATCH()
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
        case 6: prec->val = device->cnt_recv_bytes; break;
        default:
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        }
        return 0;
    }CATCH()
}

long read_rtt(aiRecord *prec)
{
    TRY {
        Guard G(device->lock);

        double sum = 0.0;
        for(size_t i=0; i<device->roundtriptimes.size(); i++) {
            sum += device->roundtriptimes[i];
        }
        sum /= device->roundtriptimes.size();

        if(prec->linr) {
            if(prec->eslo) sum *= prec->eslo;
            sum += prec->eoff;
        }
        if(prec->aslo) sum *= prec->aslo;
        sum += prec->aoff;

        prec->val = sum;
        prec->udf = isnan(sum);
        return 2;
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

long read_jblob(aaiRecord *prec)
{
    TRY {
        if(prec->nelm<16)
            throw std::runtime_error("Need NELM>=16");
        Guard G(device->lock);

        std::vector<char> *blob;
        switch(info->offset) {
        case 0: blob = &device->dev_infos; break;
        case 1: blob = &device->raw_infos; break;
        default:
            throw std::runtime_error("invalid jblob offset");
        }

        if(blob->empty()) {
            IFDBG(1, "Not connected");
        } else if(blob->size() > prec->nelm) {
            IFDBG(1, "blob size %zu exceeds NELM=%u",
                  blob->size(), (unsigned)prec->nelm);
        } else {
            memcpy(prec->bptr,
                   &(*blob)[0],
                   blob->size());

            prec->nord = blob->size();
            return 0;
        }
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);

        return ENODEV;
    }CATCH()
}

#undef TRY
#define TRY SyncInfo *info = static_cast<SyncInfo*>(prec->dpvt); if(!info) { \
    (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); return ENODEV; } \
    Device *device=info->device; (void)device; try


struct SyncInfo : public RecInfo
{
    unsigned wait_for;

    SyncInfo(dbCommon *prec, Device *dev)
        :RecInfo(prec, dev)
        ,wait_for(0u)
    {}

    void complete() override final {
        bool done = true;
        try {
            Guard G(device->lock);

            if(wait_for==0) {
                throw std::logic_error("SyncInfo too many completes");
            } else {
                wait_for--;
                if(wait_for!=0) {
                    done = false;
                    IFDBG(1, "%u remaining", wait_for);
                } IFDBG(1, "Sync'd");
            }

        }catch(std::exception& e){
            ERR("error in complete() : %s\n", e.what());
            done = true;
        }

        if(done)
            RecInfo::complete();
    }

    void cleanup() override final {
        RecInfo::cleanup();
        wait_for = 0u;
    }
};

long read_sync(longinRecord *prec)
{
    TRY {
        if(!prec->pact && info->device->active()) {
            if(device->reg_send.empty())
                IFDBG(1, "Send queue empty");

            info->wait_for = 0u;

            // add to completion list of all queued registers
            for(Device::reg_send_t::iterator it(device->reg_send.begin()), end(device->reg_send.end());
                it != end; ++it)
            {
                DevReg *reg = *it;
                assert(reg->inprogress());

                // ask to get callback after in-progress op completes
                reg->records_inprog.push_back(info);
                info->wait_for++;

                prec->pact = 1;
            }

            if(prec->pact)
                IFDBG(1, "Wait for %u registers", info->wait_for);

        } else {
            if(device->current!=Device::Running || info->wait_for)
                (void)(recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM));

            info->wait_for = 0u;
            prec->pact = 0;
            prec->val++;
            IFDBG(1, "Complete");
        }

        return 0;
    }CATCH()
}

#undef TRY
#define TRY MetaInfo *info = static_cast<MetaInfo*>(prec->dpvt); if(!info) { \
    (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); return ENODEV; } \
    Device *device=info->device; (void)device; try

struct MetaInfo : public RecInfo
{
    bool has_default;
    epicsUInt32 defval;

    MetaInfo(dbCommon *prec, Device *dev)
        :RecInfo(prec, dev)
        ,has_default(false)
        ,defval(0u)
    {}

    virtual void configure(const pairs_t& pairs) override final
    {
        has_default = get_pair(pairs, "default", defval);
    }
};

long read_metadata(longinRecord *prec)
{
    TRY {
        Guard G(device->lock);

        JBlob::info32_t::iterator it(device->info32.find(info->regname));
        if(it!=device->info32.end()) {
            prec->val = it->second;

        } else if(!info->has_default) {
            (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);

        } else {
            prec->val = info->defval;
        }

        return 0;
    } CATCH()
}

} // namespace

// device-wide settings
DSET(devSoFEEDDebug, longout, init_common<RecInfo>::fn, NULL, write_debug)
DSET(devSoFEEDAddress, stringout, init_common<RecInfo>::fn, NULL, write_address)
DSET(devSoFEEDForceErr, stringout, init_common<RecInfo>::fn, NULL, force_error)
DSET(devBoFEEDCommit, bo, init_common<RecInfo>::fn, NULL, write_commit)

// device-wide status
DSET(devMbbiFEEDDevState, mbbi, init_common<RecInfo>::fn, get_dev_changed_intr, read_dev_state)
DSET(devAaiFEEDInfo, aai, init_common<RecInfo>::fn, get_dev_changed_intr, read_jsoninfo)
DSET(devLiFEEDCounter, longin, init_common<RecInfo>::fn, get_dev_changed_intr, read_counter)
DSET(devAiFEEDrtt, ai, init_common<RecInfo>::fn, get_dev_changed_intr, read_rtt)
DSET(devAaiFEEDError, aai, init_common<RecInfo>::fn, get_dev_changed_intr, read_error)
DSET(devAaiFEEDJBlob, aai, init_common<RecInfo>::fn, get_dev_changed_intr, read_jblob)
DSET(devLiFEEDConnect, longin, init_common<RecInfo>::fn, get_on_connect_intr, read_inc)

// JSON __metadata__ info
DSET(devLiFEEDMetadata, longin, init_common<MetaInfo>::fn, get_on_connect_intr, read_metadata)

// register status
DSET(devMbbiFEEDRegState, mbbi, init_common<RecInfo>::fn, get_reg_changed_intr, read_reg_state)

// device-wide special
DSET(devLiFEEDSync, longin, init_common<SyncInfo>::fn, NULL, read_sync)
