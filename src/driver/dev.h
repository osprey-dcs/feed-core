#ifndef DEV_H
#define DEV_H

#include <ostream>
#include <map>
#include <string>

#include <dbAccess.h>
#include <dbLock.h>
#include <recSup.h>
#include <dbScan.h>
#include <recGbl.h>

#include "device.h"

#ifndef DBR_AMSG
#  define recGblSetSevrMsg(REC, STAT, SEVR, ...) recGblSetSevr(REC, STAT, SEVR)
#endif

typedef std::map<std::string, std::string> pairs_t;

void split_pairs(const char *lstr, pairs_t& pairs);
bool get_pair(const pairs_t& pairs, const std::string& key, std::string& out);
bool get_pair(const pairs_t& pairs, const std::string& key, epicsUInt32& out);
bool get_pair(const pairs_t& pairs, const std::string& key, double& out);
bool get_pair(const pairs_t& pairs, const std::string& key, bool& out);

struct ScanLock {
    dbCommon * const prec;
    ScanLock(dbCommon *prec) :prec(prec) { dbScanLock(prec); }
    ~ScanLock() { dbScanUnlock(prec); }
};

struct RecInfo : public RegInterest
{

    epicsUInt32 offset, // offset of first element (in words)
                step,   // increment between elements (in words)
                size;   // use array register data up to this element
                        // optional; default is to use full array

    double scale;

    bool commit;
    bool wait;
    // effects input records
    // false - use data from previous read op
    // true  - use data from previous write op
    bool rbv;
    // push meta-data to record
    bool meta;

    // registry of logical signal names
    typedef std::map<std::string, RecInfo*> signals_t;
    static signals_t signals;

    std::string regname;
    // our logical signal name (or empty())
    std::string signal;

    RecInfo(dbCommon *prec, Device *device);
    virtual ~RecInfo();

    // called during init_common_base()
    virtual void configure(const pairs_t& pairs);

    // reset after error/exception in dset function
    // with record locked
    virtual void cleanup();

    // RegInterest::complete()
    // record not locked
    virtual void complete() override;

    // RegInterest::show()
    // record not locked
    virtual void show(std::ostream& strm, int lvl) override;

    // RegInterest::getInfo()
    // record not locked
    virtual void getInfo(infos_t& infos) const override;
};

// Find INP/OUT
DBLINK *getDevLnk(dbCommon *prec);

long init_common_base(dbCommon *prec, RecInfo*(*buildfn)(dbCommon *prec, Device *device));

template<typename Priv>
struct init_common {
    static RecInfo *alloc_info(dbCommon *prec, Device *device) {
        return new Priv(prec, device);
    }
    static long fn(dbCommon *prec)
    {
        return init_common_base(prec, &alloc_info);
    }
};

// attach to Device::on_connect
long get_on_connect_intr(int dir, dbCommon *prec, IOSCANPVT *scan);
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
