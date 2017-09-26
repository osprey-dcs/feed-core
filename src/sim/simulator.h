#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <string>
#include <map>

#include <epicsTypes.h>
#include <osiSock.h>
#include <epicsEvent.h>
#include <epicsMutex.h>
#include <epicsThread.h>

#include "utils.h"
#include "jblob.h"

struct SimReg {
    std::string name;
    epicsUInt32 base;
    epicsUInt32 mask;
    bool readable, writable;

    typedef std::vector<epicsUInt32> storage_t;
    storage_t storage;

    SimReg() :readable(false), writable(false) {}
    SimReg(const JRegister& reg);
};

class Simulator
{
public:
    typedef std::map<std::string, epicsUInt32> values_t;

    explicit Simulator(const osiSockAddr& ep,
                       const JBlob& blob,
                       const values_t& initial);
    ~Simulator();

    void add(const SimReg& reg);

    void endpoint(osiSockAddr&);
    void exec();
    void interrupt();

    SimReg& operator[](const std::string& name);

    bool debug;
    epicsMutex lock;
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

#endif // SIMULATOR_H
