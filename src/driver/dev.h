#ifndef DEV_H
#define DEV_H

#include <ostream>
#include <map>
#include <string>

#include <dbAccess.h>
#include <dbLock.h>
#include <recSup.h>
#include <dbScan.h>

#include "device.h"

typedef std::map<std::string, std::string> pairs_t;

void split_pairs(const char *lstr, pairs_t& pairs);
bool get_pair(const pairs_t& pairs, const std::string& key, std::string& out);
bool get_pair(const pairs_t& pairs, const std::string& key, epicsUInt32& out);
bool get_pair(const pairs_t& pairs, const std::string& key, double& out);
bool get_pair(const pairs_t& pairs, const std::string& key, bool& out);

struct RecInfo : public RegInterest
{
    dbCommon * const prec;
    Device * const device;

    epicsUInt32 offset, // offset of first element (in words)
                step;   // increment between elements (in words)

    bool autocommit;
    bool wait;

    RecInfo(dbCommon *prec, Device *device)
        :prec(prec)
        ,device(device)
        ,offset(0u)
        ,step(1)
        ,autocommit(true), wait(true)
    {}

    virtual void configure(const pairs_t& pairs) {
        get_pair(pairs, "offset", offset);
        get_pair(pairs, "step", step);
        get_pair(pairs, "autocommit", autocommit);
        get_pair(pairs, "wait", wait);
    }

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

    virtual void getInfo(infos_t&);
};

// Find INP/OUT
DBLINK *getDevLnk(dbCommon *prec);

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

        info->configure(pairs);

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

// attach to Device::current_changed
long get_dev_changed_intr(int dir, dbCommon *prec, IOSCANPVT *scan);
// attach to DevReg::changed
long get_reg_changed_intr(int dir, dbCommon *prec, IOSCANPVT *scan);


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
    extern "C" {epicsExportAddress(dset, NAME);}

#endif // DEV_H
