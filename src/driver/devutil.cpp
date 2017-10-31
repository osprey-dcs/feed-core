#include <iostream>

#include <epicsStdlib.h>
//#include <dbAccess.h>
#include <dbStaticLib.h>

#include "dev.h"

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

void RecInfo::getInfo(infos_t& infos)
{
    DBENTRY entry;

    if(!reg)
        return;

    infos_t::iterator it(infos.find(reg->info.name));
    if(it==infos.end()) {
        infos[reg->info.name];
        it = infos.find(reg->info.name);
        assert(it!=infos.end());
    }
    info_items_t& items = it->second;

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
