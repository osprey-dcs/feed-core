#ifndef DEVICE_H
#define DEVICE_H

#include <string>
#include <list>
#include <map>
#include <deque>

#include <epicsMutex.h>
#include <epicsGuard.h>
#include <epicsThread.h>
#include <epicsTime.h>

#include <dbCommon.h>
#include <dbScan.h>

#include "jblob.h"
#include "utils.h"

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

struct Device;
struct DevReg;

struct RegInterest
{
    DevReg *reg;
    IOSCANPVT changed;
    RegInterest();
    virtual ~RegInterest() {}
    virtual void complete() =0;
    virtual void show(std::ostream&, int lvl) {}
};

struct DevReg
{
    DevReg(Device *dev, const JRegister& info, bool bootstrap = false);

    Device * const dev;
    const JRegister info;
    const bool bootstrap;

    enum state_t {Invalid, //!< Idle, w/o valid data
                  InSync,  //!< Idle, w/ valid data
                  Reading, //!< Read in progress
                  Writing, //!< Write in progress
                 } state;

    bool inprogress() const { return state==Reading || state==Writing; }

    typedef std::vector<epicsUInt32> mem_t;
    // storage for this register.  Kept in network byte order
    mem_t mem;

    typedef std::vector<bool> flags_t;
    // track which addresses have been received
    flags_t received;
    // optimization.  a count of the # cleared bits in 'received'
    size_t nremaining;

    // next offset (in .mem) to send
    epicsUInt32 next_send;

    // time last received (read or write)
    epicsTime rx;

    typedef std::vector<RegInterest*> interested_t;
    interested_t interested;
    void scan_interested();

    // async. record processing waiting on this register
    short stat, sevr;
    typedef std::list<RegInterest*> records_t;
    records_t records;

    void process();

    // queue to be sent
    bool queue(bool write);

    void show(std::ostream& strm, int lvl) const;
};

struct DevMsg
{
    static const unsigned nreg = 127;

    enum state_t {Free, Ready, Sent} state;

    // sequence number used (when Sent)
    epicsUInt32 seq;

    DevReg* reg[nreg];

    std::vector<epicsUInt32> buf;

    // timeout if no reply by this time
    epicsTime due;

    DevMsg() { clear(); }
    void clear() {
        state = Free;
        seq = 0;
        memset(reg, 0, sizeof(reg));
        buf.clear();
    }
    void show(std::ostream& strm, int lvl) const;
};

struct Device : public epicsThreadRunable
{
    Device(const std::string& name, osiSockAddr &ep);
    ~Device();

    mutable epicsMutex lock;

    Socket sock, wakeupRx, wakeupTx;

    const std::string myname;
    std::string peer_name;
    osiSockAddr peer_addr;

    // debug print bit mask
    // 0x01 - packet validation
    // 0x02 - packet handling (eg. timeout)
    // 0x04 - ROM informational
    // 0x08 - State machine
    // 0x10 - Worker thread
    epicsUInt32 debug;

    enum state_t {
        Error,      //!< Something bad happened
        Idle,       //!< Un-addressed
        Searching,  //!< poll/wait for first reply
        Inspecting, //!< Retrieve/process ROM blob
        Running,    //!< Normal operation
    } current;
    static const char *current_name[5];

    inline bool active() const { return current!=Error && current!=Idle; }

    IOSCANPVT current_changed;

    epicsUInt32 cnt_sent,
                cnt_recv,
                cnt_ignore,
                cnt_timo,
                cnt_err,
                send_seq;

    std::string last_message;

    typedef std::map<std::string, DevReg*> reg_by_name_t;
    reg_by_name_t reg_by_name;

    typedef std::deque<DevReg*> reg_send_t;
    reg_send_t reg_send;

    typedef std::multimap<std::string, RegInterest*> reg_interested_t;
    reg_interested_t reg_interested;

    DevReg::records_t records;

    // special registers which low level code knows about
    feed::auto_ptr<DevReg> reg_rom, reg_id;

    std::vector<DevMsg> inflight;

    bool want_to_send;
    bool runner_stop;
    bool reset_requested;
    epicsThread runner;

    void poke_runner();

    void request_reset();
    void reset();

    void handle_send(Guard &G);
    void handle_process(const std::vector<char>& buf, PrintAddr& addr);
    void handle_timeout();
    void handle_inspect();
    void handle_state();

    virtual void run();

    void show(std::ostream& strm, int lvl) const;

    static epicsMutex devices_lock;
    typedef std::map<std::string, Device*> devices_t;
    static devices_t devices;
};

extern int feedNumInFlight;
extern double feedTimeout;

#endif // DEVICE_H
