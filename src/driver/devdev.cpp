
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

#include <epicsExport.h>

namespace {

struct RecInfo : public RegInterest
{
    dbCommon * const prec;
    Device * const device;

    epicsUInt32 offset;
    epicsUInt32 mask;
    epicsUInt32 value;
    double retry;

    bool autocommit;
    bool wait;

    RecInfo(dbCommon *prec, Device *device)
        :prec(prec)
        ,device(device)
        ,offset(0u), mask(0u), value(0u)
        ,retry(1.0)
        ,autocommit(true), wait(true)
    {}

    // reset after error/exception in dset function
    virtual void cleanup() {
        prec->pact = 0;
    }

    virtual void complete() {
        long (*process)(dbCommon*) = (long (*)(dbCommon*))prec->rset->process;
        dbScanLock(prec);
        (*process)(prec); // ignore result
        dbScanUnlock(prec);
    }

    virtual void show(std::ostream& strm, int lvl) {
        strm<<prec->name;
    }
};

typedef std::map<std::string, std::string> pairs_t;

void split_pairs(const char *lstr, pairs_t& pairs)
{
    while(*lstr==' ' || *lstr=='\t') lstr++; // skip leading whitespace

    while(*lstr) {
        const char *start = lstr; // start of token

        // find end of token
        while(*lstr!='\0' && *lstr!='=' && *lstr!=' ' && *lstr!='\t') lstr++;

        std::string key(start, lstr-start);
        if(key.empty())
            throw std::runtime_error("Zero length key value not allowed");

        while(*lstr==' ' || *lstr=='\t') lstr++; // WS between token and '='

        if(*lstr!='=')
            throw std::runtime_error(SB()<<"Missing expected '=' at \""<<lstr<<"\"");

        lstr++; // skip '='

        while(*lstr==' ' || *lstr=='\t') lstr++; // WS between '=' and token

        start = lstr;

        // find end of token
        while(*lstr!='\0' && *lstr!=' ' && *lstr!='\t') lstr++;

        std::string value(start, lstr-start);

        pairs[key] = value;

        while(*lstr==' ' || *lstr=='\t') lstr++; // WS between pairs
    }
}

bool get_pair(const pairs_t& pairs, const std::string& key, std::string& out)
{
    pairs_t::const_iterator it = pairs.find(key);
    if(it==pairs.end())
        return false;
    out = it->second;
    return true;
}

bool get_pair(const pairs_t& pairs, const std::string& key, epicsUInt32& out)
{
    pairs_t::const_iterator it = pairs.find(key);
    if(it==pairs.end())
        return false;
    else if(epicsParseUInt32(it->second.c_str(), &out, 0, 0))
        throw std::runtime_error("Error parsing integer");
    return true;
}

bool get_pair(const pairs_t& pairs, const std::string& key, double& out)
{
    pairs_t::const_iterator it = pairs.find(key);
    if(it==pairs.end())
        return false;
    else if(epicsParseDouble(it->second.c_str(), &out, 0))
        throw std::runtime_error("Error parsing double");
    return true;
}

bool get_pair(const pairs_t& pairs, const std::string& key, bool& out)
{
    pairs_t::const_iterator it = pairs.find(key);
    if(it==pairs.end())
        return false;
    else if(it->second=="true")
        out = true;
    else if(it->second=="false")
        out = false;
    else
        throw std::runtime_error("Expected 'true' or 'false'");
    return true;
}

DBLINK *getDevLnk(dbCommon *prec)
{
    DBENTRY entry;
    dbInitEntry(pdbbase, &entry);

    if(dbFindRecord(&entry, prec->name))
        throw std::logic_error("Failed to find myself");

    if(dbFindField(&entry,"INP")!=0 && dbFindField(&entry,"OUT")!=0)
        throw std::logic_error("Failed to find INP/OUT");

    DBLINK *ret = (DBLINK*)entry.pfield;

    dbFinishEntry(&entry);
    return ret;
}

#define IFDBG(N, FMT, ...) if(prec->tpro>(N)) printf("%s %s : " FMT "\n", logTime(), prec->name, ##__VA_ARGS__)

template<typename Priv>
long init_common(dbCommon *prec)
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

        std::string name;
        if(!get_pair(pairs, "name", name))
            throw std::runtime_error("Omitted required key name=");

        Device::devices_t::iterator it = Device::devices.find(name);
        if(it==Device::devices.end()) {
            osiSockAddr iface;
            iface.ia.sin_family = AF_INET;
            iface.ia.sin_addr.s_addr = htonl(INADDR_ANY);
            iface.ia.sin_port = htons(0);

            feed::auto_ptr<Device> dev(new Device(name, iface));

            std::pair<Device::devices_t::iterator, bool> P;
            P = Device::devices.insert(std::make_pair(name, dev.get()));
            assert(P.second);
            it = P.first;
            dev.release();

            PrintAddr addr(iface);

            std::cout<<"# Create FEED Device "<<name<<"\n"
                       "# Listening @ "<<addr.c_str()<<"\n";
        }

        feed::auto_ptr<Priv> info(new Priv(prec, it->second));

        get_pair(pairs, "offset", info->offset);
        get_pair(pairs, "mask", info->mask);
        get_pair(pairs, "value", info->value);
        get_pair(pairs, "retry", info->retry);
        get_pair(pairs, "autocommit", info->autocommit);
        get_pair(pairs, "wait", info->wait);

        std::string regname;
        if(get_pair(pairs, "reg", regname))
        {
            info->device->reg_interested.insert(std::make_pair(regname, info.get()));
            IFDBG(1, "Attach to %s", regname.c_str());

            Device::reg_by_name_t::iterator it(info->device->reg_by_name.find(regname));
            if(it!=info->device->reg_by_name.end() && it->second->bootstrap) {
                // bootstrap registers connected immediately and perpetually
                info->reg = it->second;
                it->second->interested.push_back(info.get());
                IFDBG(1, "Attach to bootstrap");
            }
        } else IFDBG(1, "No register named in: %s", lstr);

        prec->dpvt = info.release();
        return 0;
    } catch(std::exception& e){
        fprintf(stderr, "%s: Error %s\n", prec->name, e.what());
        return -EIO;
    }
}

#undef IFDBG
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

struct WaitInfo : public RecInfo
{

    bool cb_inprogress;
    CALLBACK cb;

    WaitInfo(dbCommon *prec, Device *dev)
        :RecInfo(prec, dev)
        ,cb_inprogress(false)
    {
        callbackSetCallback(&expire, &cb);
        callbackSetPriority(prec->prio, &cb);
        callbackSetUser(this, &cb);
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

template<typename REC>
struct dset6 {
    long N;
    long (*report)(int);
    long (*init)(int);
    long (*init_record)(dbCommon*);
    long (*get_iointr_info)(int detach, dbCommon*, IOSCANPVT*);
    long (*readwrite)(REC*);
    long (*extra)(dbCommon*);
};
#define DSET(NAME, REC, INIT_REC, GET_IO, RW) \
    static const dset6<REC##Record> NAME = {6, NULL, NULL, INIT_REC, GET_IO, RW, NULL}; \
    epicsExportAddress(dset, NAME);

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

// register special
DSET(devBoFEEDWatchReg, bo, init_common<WaitInfo>, NULL, write_test_mask)
