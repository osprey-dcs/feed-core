#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <string>
#include <map>

#include <epicsTypes.h>
#include <osiSock.h>
#include <epicsEvent.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <shareLib.h>

#include "utils.h"
#include "jblob.h"

struct SimReg {
    std::string name;
    epicsUInt32 base;
    epicsUInt32 mask; // data bit mask
    bool readable, writable;

    // simulate storage is in host byte order
    typedef std::vector<epicsUInt32> storage_t;
    storage_t storage;

    SimReg() :base(0), mask(0), readable(false), writable(false) {}
    explicit SimReg(const JRegister& reg);

    inline size_t size() const { return storage.size(); }
    inline epicsUInt32& operator[](size_t idx) { return storage.at(idx); }
    inline const epicsUInt32& operator[](size_t idx) const { return storage.at(idx); }
};

class epicsShareClass Simulator
{
public:
    typedef std::map<epicsUInt32, epicsUInt32> values_t;

    /* Create simulator listening at given endpoint (address+port).
     * Provides registers listed in JBlob.
     * Initial address values taken from initial map (except ROM)
     */
    Simulator(const osiSockAddr& ep,
              const JBlob& blob,
              const values_t& initial);
    virtual ~Simulator();

    // manually add a register (in addition to those defined in JBlob)
    void add(const SimReg& reg);

    // read back the actual endpoint.
    // eg. after passing port 0 to ctor, use this to find the randomly assigned port
    void endpoint(osiSockAddr&);
    // begin receiving and processing messages.
    // Doesn't return until interrupt() is called
    void exec();
    // cause exec() to return.
    // Calls to interrupt() are queued.
    void interrupt();

    // access register information.
    // take lock to modify register contents
    SimReg& operator[](const std::string& name);

    // turns on some extra debug prints
    bool debug;
    // arbitrary slowdown before sending reply
    double slowdown;
    // guard access  to register values
    epicsMutex lock;
protected:
    virtual void reg_write(SimReg& reg, epicsUInt32 offset, epicsUInt32 newval);

private:
    bool running;

    typedef std::map<std::string, SimReg> reg_by_name_t;
    reg_by_name_t reg_by_name;

    // borrows storage from reg_by_name
    typedef std::map<epicsUInt32, SimReg*> reg_by_addr_t;
    reg_by_addr_t reg_by_addr;

    Socket serve, wakeupRx, wakeupTx;
    osiSockAddr serveaddr;

public:
    typedef reg_by_name_t::iterator iterator;
    iterator begin() { return reg_by_name.begin(); }
    iterator end() { return reg_by_name.end(); }
};

class epicsShareClass Simulator_RFS : public Simulator
{
public:
    Simulator_RFS(const osiSockAddr& ep,
                  const JBlob& blob,
                  const values_t& initial);
    virtual ~Simulator_RFS();

private:
    // registers use by logic
    SimReg& circle_buf_flip;
    SimReg& llrf_circle_ready;
    SimReg* shell_X_dsp_chan_keep[2];
    SimReg* shell_X_dsp_tag[2];
    SimReg* shell_X_circle_data[2];
    SimReg* shell_X_slow_data[2];

    double phase;
    epicsUInt16 circle_count;

    virtual void reg_write(SimReg& reg, epicsUInt32 offset, epicsUInt32 newval) override final;
    void acquire(unsigned instance);
};

class epicsShareClass Simulator_HIRES : public Simulator
{
public:
    Simulator_HIRES(const osiSockAddr& ep,
                  const JBlob& blob,
                  const values_t& initial);
    virtual ~Simulator_HIRES();

private:
    struct WF {
        SimReg *buffer;
        SimReg *reset;
        SimReg *status;
        SimReg *mask;
        epicsUInt32 valid;
        unsigned reset_bit;
        unsigned status_bit;
        epicsUInt32 seed;
        WF()
            :buffer(0), reset(0), status(0), mask(0)
            ,reset_bit(0u), status_bit(0u)
            ,seed(0u)
        {}
        void process();
    };
    WF banyan,
       trace_odata,
       decay_data,
       abuf_data,
       adcbuf_dataB;

    virtual void reg_write(SimReg& reg, epicsUInt32 offset, epicsUInt32 newval) override final;
};

#endif // SIMULATOR_H
