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

    dbCommon * const prec;
    Device * const device;

    DevReg *reg;
    IOSCANPVT changed;
    RegInterest(dbCommon *prec, Device *dev);
    virtual ~RegInterest() {}
    // callback after register read/write is complete
    virtual void complete() =0;
    // debug
    virtual void show(std::ostream& strm, int lvl) {}
    // called when building Device::dev_info
    virtual void getInfo(infos_t& infos) const {}
    // called after (re)connect.  In Inspecting state, just prior to Running
    virtual void connected() {};
};

// Device Register
struct DevReg
{
    DevReg(Device *dev, const JRegister& info, bool bootstrap = false);
    ~DevReg();

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

    bool read_queued, write_queued;

    bool inprogress() const { return state==Reading || state==Writing; }

    typedef std::vector<epicsUInt32> mem_t;
    // storage for this register.  Kept in network byte order
    mem_t mem_rx, // recv cache
          mem_tx; // send cache

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
    records_t records_inprog, // actions waiting on the currently executing op
              records_write,  // actions waiting on the queued write
              records_read;   // actions waiting on the queued read

    void reset();

    void process(bool cancel);

    // queue to be sent
    void queue(bool write, RegInterest* action=0);

    void show(std::ostream& strm, int lvl) const;
};

struct DevMsg
{
    // max. ops per message.  Based on 1500 ethernet MTU assuming no IP header options
    //    1500 >= Headers + (180 + 1)*8
    //    Ethernet+IP+UDP headers <= 52 bytes
    // If this is too large (IP header has options) then messages will be fragmented,
    // which our devices don't know how to reassemble...
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
    // 0 0x01 - packet validation
    // 1 0x02 - packet handling (eg. timeout)
    // 2 0x04 - ROM informational
    // 3 0x08 - State machine
    // 4 0x10 - Worker thread
    // 5 0x20 - Register queuing
    // 6 0x40 - dset
    epicsUInt32 debug;

    enum state_t {
        Error,      //!< Something bad happened
        Idle,       //!< Un-addressed
        Searching,  //!< poll/wait for first reply
        Inspecting, //!< Retrieve/process ROM blob
        Running,    //!< Normal operation
    } current;
    static const char *current_name[5];

    inline bool active() const { return current!=Error && current!=Idle && current!=Searching; }

    IOSCANPVT current_changed, on_connect;

    // optimization. time at which current loop iteration "starts"
    // eg. worker thread start, or poll() returns
    epicsTime loop_time;

    std::vector<char> dev_infos, // compressed json blob of our info.
                      raw_infos; // compressed json blob of raw info.

    epicsUInt32 cnt_sent,
                cnt_recv,
                cnt_recv_bytes,
                cnt_ignore,
                cnt_timo,
                cnt_err,
                send_seq;

    std::vector<double> roundtriptimes;
    size_t rtt_ptr;

    std::string last_message;

    // non-JSON info extracted from ROM
    std::string description, jsonhash, codehash;

    // __metadata__ keys from JSON
    JBlob::info32_t info32;

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
    feed::auto_ptr<DevReg> reg_rom2, reg_rom16, reg_id;

    std::vector<DevMsg> inflight;

    // whether we should poll() to see if send() would block
    bool want_to_send;
    bool runner_stop;
    bool reset_requested, // request to transition to Idle on next iteration
         error_requested;   // request transition to Error on next iteration
    bool after_reset;

    epicsThread runner;

    // cause runner to end poll() early.
    // call after queuing for send
    void poke_runner();

    void request_reset();
    void reset(bool error=false);

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
    void handle_inspect(Guard &G);
    // state machine logic
    void handle_state(Guard &G);

    // main loop
    virtual void run() override final;

    void show(std::ostream& strm, int lvl) const;

    static epicsMutex devices_lock;
    typedef std::map<std::string, Device*> devices_t;
    static devices_t devices;
};

extern int feedNumInFlight;
extern double feedTimeout;
extern int feedUDPHeaderSize;
extern int feedUDPPortNum;

#endif // DEVICE_H
