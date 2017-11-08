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
#include <shareLib.h>

#include "jblob.h"
#include "utils.h"

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

struct Device;
struct DevReg;

// Something (a PDB record) which wants to access register data
struct RegInterest
{
    // keys will be quoted, values are raw json
    typedef std::map<std::string, std::string> info_items_t;
    typedef std::map<std::string, info_items_t> infos_t;

    DevReg *reg;
    IOSCANPVT changed;
    RegInterest();
    virtual ~RegInterest() {}
    // callback after register read/write is complete
    virtual void complete() =0;
    // debug
    virtual void show(std::ostream&, int lvl) {}
    // called when building Device::dev_info
    virtual void getInfo(infos_t&) {}
};

// Device Register
struct DevReg
{
    DevReg(Device *dev, const JRegister& info, bool bootstrap = false);

    Device * const dev;
    const JRegister info;
    // bootstrap (aka automatic or implicit) register?
    // if yes, then not removed on disconnect/timeout
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

    // Records associated with this register
    // Triggers when SCAN=I/O Intr
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
    static const unsigned nreg = 180;

    enum state_t {Free, Ready, Sent} state;

    // sequence number used (when Sent)
    epicsUInt32 seq;

    // registers associated with this message.
    // some may be NULL if message shorter than max.
    // Does not include padding reads for really short messages
    DevReg* reg[nreg];

    // packet construction buffer
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

struct epicsShareClass Device : public epicsThreadRunable
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

    IOSCANPVT current_changed, on_connect;

    // optimization. time at which current loop iteration "starts"
    // eg. worker thread start, or poll() returns
    epicsTime loop_time;

    // compressed json blob of our info.
    std::vector<char> dev_infos;

    epicsUInt32 cnt_sent,
                cnt_recv,
                cnt_ignore,
                cnt_timo,
                cnt_err,
                send_seq;

    std::string last_message;

    typedef std::map<std::string, DevReg*> reg_by_name_t;
    reg_by_name_t reg_by_name;

    // list of registers queued to be sent.
    // front() entry is currently being sent
    typedef std::deque<DevReg*> reg_send_t;
    reg_send_t reg_send;

    // keep track of all interestes.
    // those current w/ an assocation, and those without
    typedef std::multimap<std::string, RegInterest*> reg_interested_t;
    reg_interested_t reg_interested;

    // async. records to complete in next loop iteration
    DevReg::records_t records;

    // special registers which low level code knows about
    feed::auto_ptr<DevReg> reg_rom, reg_id;

    std::vector<DevMsg> inflight;

    // whether we should poll() to see if send() would block
    bool want_to_send;
    bool runner_stop;
    // request to transition to Idle on next iteration
    bool reset_requested;

    epicsThread runner;

    // cause runner to end poll() early.
    // call after queuing for send
    void poke_runner();

    void request_reset();
    void reset();

    // handle_* called from run().

    // send as much as possible (empty reg_send to fill inflight)
    void handle_send(Guard &G);
    // process a single received packet
    void handle_process(const std::vector<char>& buf, PrintAddr& addr);
    // check for timeout of inflight requests
    void handle_timeout();
    // timeout inflight[i]
    void do_timeout(unsigned i);
    // process ROM and prepare for transition to Running
    void handle_inspect();
    // state machine logic
    void handle_state();

    // main loop
    virtual void run();

    void show(std::ostream& strm, int lvl) const;

    static epicsMutex devices_lock;
    typedef std::map<std::string, Device*> devices_t;
    static devices_t devices;
};

extern int feedNumInFlight;
extern double feedTimeout;

#endif // DEVICE_H
