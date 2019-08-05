#include <stdexcept>
#include <iostream>

#include <epicsStdlib.h>
//#include <dbAccess.h>
#include <dbStaticLib.h>

#include "dev.h"

RecInfo::signals_t RecInfo::signals;

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

#define IFDBG(N, FMT, ...) if(prec->tpro>(N)) printf("%s %s : " FMT "\n", logTime(), prec->name, ##__VA_ARGS__)

long init_common_base(dbCommon *prec, RecInfo*(*buildfn)(dbCommon *prec, Device *device))
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

        feed::auto_ptr<RecInfo> info((*buildfn)(prec, it->second));

        info->configure(pairs);

        if(get_pair(pairs, "reg", info->regname))
        {
            info->device->reg_interested.insert(std::make_pair(info->regname, info.get()));
            IFDBG(1, "Attach to %s", info->regname.c_str());

            Device::reg_by_name_t::iterator it(info->device->reg_by_name.find(info->regname));
            if(it!=info->device->reg_by_name.end() && it->second->bootstrap) {
                // bootstrap registers connected immediately and perpetually
                info->reg = it->second;
                it->second->interested.push_back(info.get());
                IFDBG(2, "Attach to bootstrap");
            }
        } else IFDBG(2, "No register named in: %s", lstr);

        if(get_pair(pairs, "signal", info->signal))
        {
            RecInfo::signals_t::iterator it(RecInfo::signals.find(info->signal));

            if(it!=RecInfo::signals.end()) {
                fprintf(stderr, "%s: error : signal name already used by %s\n",
                        prec->name, it->second->prec->name);
                info->signal.clear();

            } else {
                RecInfo::signals[info->signal] = info.get();
                IFDBG(1, "is signal %s", info->signal.c_str());
            }
        } else IFDBG(2, "No signal");

        prec->dpvt = info.release();
        return 0;
    } catch(std::exception& e){
        fprintf(stderr, "%s: Error %s\n", prec->name, e.what());
        return -EIO;
    }
}

#undef IFDBG

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


RecInfo::RecInfo(dbCommon *prec, Device *device)
    :RegInterest(prec, device)
    ,offset(0u)
    ,step(1)
    ,scale(1.0)
    ,wait(true)
    ,rbv(false)
{}

RecInfo::~RecInfo()
{
    if(!signal.empty()) {
        signals.erase(signal);
    }
}

void RecInfo::configure(const pairs_t& pairs) {
    get_pair(pairs, "offset", offset);
    get_pair(pairs, "step", step);
    get_pair(pairs, "scale", scale);
    get_pair(pairs, "wait", wait);
    get_pair(pairs, "rbv", rbv);
}

void RecInfo::cleanup() {
    prec->pact = 0;
}

void RecInfo::complete() {
    long (*process)(dbCommon*) = (long (*)(dbCommon*))prec->rset->process;
    dbScanLock(prec);
    if(prec->pact) {
        (*process)(prec); // ignore result
    }
    dbScanUnlock(prec);
}

void RecInfo::show(std::ostream& strm, int lvl) {
    strm<<prec->name;
}

void RecInfo::getInfo(infos_t& infos) const
{
    DBENTRY entry;

    if(regname.empty())
        return;

    info_items_t& items = infos[regname];

    items["present"] = reg ? "true" : "false";

    dbInitEntry(pdbbase, &entry);

    if(dbFindRecord(&entry, prec->name))
        throw std::logic_error("Can't find myself");

    if(!dbFirstInfo(&entry)) {
        do {
            const char *name = dbGetInfoName(&entry);

            if(strncmp(name, "feed:info:", 10)!=0)
                continue;
            name += 10;

            items[name] = SB()<<"\""<<dbGetInfoString(&entry)<<"\"";

        }while(!dbNextInfo(&entry));
    }

    dbFinishEntry(&entry);

}
