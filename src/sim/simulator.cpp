#include <iostream>
#include <stdexcept>
#include <vector>
#include <map>

#include <poll.h>

#include <errlog.h>

#include "simulator.h"
#include "device.h"

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

namespace {
const size_t pkt_size_limit = (DevMsg::nreg+1)*8;
}

SimReg::SimReg(const JRegister& reg)
    :name(reg.name)
    ,base(reg.base_addr)
    ,mask((epicsUInt64(1u)<<reg.data_width)-1)
    ,readable(reg.readable)
    ,writable(reg.writable)
    ,storage(epicsUInt64(1u)<<reg.addr_width, 0u)
{
    if(base > 0xffffffff - (storage.size() - 1u))
        throw std::runtime_error(SB()<<name<<" has inconsistent base "<<base<<" and addr_width "<<reg.addr_width);
}

Simulator::Simulator(const osiSockAddr& ep, const JBlob& blob, const values_t &initial)
    :debug(false)
    ,slowdown(0.0)
    ,running(false)
    ,serveaddr(ep)
{
    for(JBlob::const_iterator it=blob.begin(), end=blob.end(); it!=end; ++it)
    {
        add(SimReg(it->second));
    }

    {
        SimReg temp;
        temp.name = "Hello";
        temp.base = 0;
        temp.mask = 0xffffffff;
        temp.readable = true;
        temp.storage.resize(4);
        temp.storage[0] = 0x48656c6c;
        temp.storage[1] = 0x6f20576f;
        temp.storage[2] = 0x726c6421;
        temp.storage[3] = 0x0d0a0d0a;
        add(temp);
    }

    {
        SimReg temp;
        temp.name = "ROM";
        temp.base = 0x0800;
        temp.mask = 0xffff;
        temp.readable = true;
        temp.storage.resize(0x800);
        add(temp);
    }

    const SimReg& romreg((*this)["ROM"]);

    for(values_t::const_iterator it=initial.begin(), end=initial.end(); it!=end; ++it)
    {
        reg_by_addr_t::iterator rit = reg_by_addr.find(it->first);
        if(rit==reg_by_addr.end()) {
            std::cout<<"Can't initialize non-existant register "<<std::hex<<it->first<<"\n";
            continue;
        }
        SimReg& reg = *rit->second;
        if(&reg==&romreg)
            continue; // don't override rom
        assert(reg.base!=0x800);

        epicsUInt32 offset = rit->first - reg.base;

        reg.storage[offset] = it->second;
    }

    {
        Socket temp(AF_INET, SOCK_DGRAM, 0);
        temp.bind(serveaddr);
        serve.swap(temp);
    }

    Socket::pipe(wakeupRx, wakeupTx);
}

Simulator::~Simulator()
{
    {
        Guard G(lock);
        if(running)
            std::cerr<<"Stop Simulator before destruction";
    }
}

void Simulator::add(const SimReg& reg)
{
    Guard G(lock);

    std::pair<reg_by_name_t::iterator, bool> P(reg_by_name.insert(std::make_pair(reg.name, reg)));
    if(!P.second)
        throw std::runtime_error(SB()<<"Duplicate register name: "<<reg.name);

    for(epicsUInt32 addr = reg.base, endaddr = reg.base+reg.storage.size(); addr<endaddr; addr++)
    {
        reg_by_addr[addr] = &P.first->second;
    }
}

void Simulator::endpoint(osiSockAddr& ep)
{
    ep = serveaddr;
}

void Simulator::exec()
{
    Guard G(lock);
    if(running)
        throw std::logic_error("Already running");

    std::vector<char> buf;
    buf.reserve(pkt_size_limit);

    try {
        running = true;

        pollfd fds[2];
        fds[0].fd = serve;
        fds[1].fd = wakeupRx;

        bool stop = false;

        while(!stop) {

            bool process = false;
            osiSockAddr peer;

            {
                double sd = slowdown;
                UnGuard U(G);

                fds[0].events = POLLIN;
                fds[1].events = POLLIN;
                fds[0].revents = fds[1].revents = 0; // paranoia

                int ret = ::poll(fds, 2, -1);
                if(ret<0)
                    throw SocketError(SOCKERRNO);

                if(fds[1].revents&(POLLERR|POLLHUP)) {
                    throw std::runtime_error("socket error from wakeupRx");
                }

                if(fds[0].revents&POLLERR) {
                    throw std::runtime_error("socket error from serve");
                }

                if(fds[1].revents&POLLIN) {
                    fds[1].revents &= ~POLLIN;

                    char temp;
                    wakeupRx.recvall(&temp, 1);
                    stop = true;
                }


                if(fds[0].revents&POLLIN) {
                    fds[0].revents &= ~POLLIN;

                    buf.resize(pkt_size_limit);

                    serve.recvfrom(peer, buf);
                    process = true;
                }

                if(fds[0].revents || fds[1].revents) {
                    std::cerr<<"poll() events unhandled  "<<std::hex<<fds[0].revents<<" "<<std::hex<<fds[1].revents<<"\n";
                }

                if(!stop && sd > 0.0) {
                    epicsThreadSleep(sd);
                }
            }
            // locked again

            PrintAddr addr(peer);

            if(process && buf.size()<32) {
                errlogPrintf("%s: ignoring too short request (%u bytes)\n",
                             addr.c_str(), unsigned(buf.size()));
                process = false;
            }

            if(process) {
                bool ignore = false;

                if(buf.size()%8) {
                    errlogPrintf("%s: sent request with %u bytes of trailing junk\n",
                                 addr.c_str(), unsigned(buf.size()%8));
                }

                // build reply in place
                // leave header alone and start with first command at offset 8

                for(size_t i=8; i+8<=buf.size(); i+=8) {
                    epicsUInt32 cmd_addr = ntohl(*reinterpret_cast<const epicsUInt32*>(&buf[i]));
                    epicsUInt32 data = ntohl(*reinterpret_cast<const epicsUInt32*>(&buf[i+4]));

                    reg_by_addr_t::iterator it(reg_by_addr.find(cmd_addr&0x00ffffff));

                    if(cmd_addr&0xef000000) {
                        errlogPrintf("%s: unused bits set in cmd/address %08x\n", addr.c_str(), unsigned(cmd_addr));
                    }

                    if(it==reg_by_addr.end()) {
                        errlogPrintf("%s: unknown cmd/address %08x\n", addr.c_str(), unsigned(cmd_addr));

                        if(cmd_addr&0x10000000) {
                            data = 0xabadface;
                        }

                    } else {
                        SimReg& reg = *it->second;
                        size_t offset = (cmd_addr&0x00ffffff)-reg.base;

                        if(reg.name=="ghost") {
                            // magic register name to test timeout handling
                            ignore = true;
                        }

                        if(cmd_addr&0x10000000) {
                            // read
                            data = reg.storage[offset];
                            if(debug)
                                errlogPrintf("%s: read %s[%u] (%06x) -> %08x\n",
                                             addr.c_str(),
                                             reg.name.c_str(), unsigned(offset),
                                             unsigned(cmd_addr), unsigned(data));

                            if(!reg.readable) {
                                errlogPrintf("%s: read of unreadable cmd/address %08x\n", addr.c_str(), unsigned(cmd_addr));
                                data = 0xdeadbeef;
                            }

                        } else {
                            // write
                            // echo back value written
                            reg_write(reg, offset, data & reg.mask);
                            if(debug)
                                errlogPrintf("%s: write %s[%u] (%06x) <- %08x\n",
                                             addr.c_str(),
                                             reg.name.c_str(), unsigned(offset),
                                             unsigned(cmd_addr), unsigned(reg.storage[offset]));

                            if(!reg.writable) {
                                errlogPrintf("%s: write of unwriteable cmd/address %08x\n", addr.c_str(), unsigned(cmd_addr));

                            }
                        }
                    }

                    *reinterpret_cast<epicsUInt32*>(&buf[i+4]) = htonl(data);
                }

                if(!ignore) {
                    UnGuard U(G);

                    serve.sendto(peer, buf);
                }
            }
        }

        running = false;
    }catch(...){
        running = false;
        throw;
    }
}

void Simulator::reg_write(SimReg& reg, epicsUInt32 offset, epicsUInt32 newval)
{
    reg.storage[offset] = newval & reg.mask;
}

void Simulator::interrupt()
{
    char c = '!';

    wakeupTx.sendall(&c, 1);
}

SimReg& Simulator::operator[](const std::string& name)
{
    Guard G(lock);
    reg_by_name_t::iterator it(reg_by_name.find(name));
    if(it==reg_by_name.end())
        throw std::runtime_error(SB()<<"No register "<<name);
    return it->second;
}
