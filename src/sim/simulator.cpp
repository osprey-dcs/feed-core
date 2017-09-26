#include <iostream>
#include <stdexcept>
#include <vector>
#include <map>

#include <poll.h>

#include <errlog.h>

#include "simulator.h"

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

namespace {
const size_t pkt_size_limit = 128*8;
}

SimReg::SimReg(const JRegister& reg)
    :name(reg.name)
    ,base(reg.base_addr)
    ,mask((1u<<reg.data_width)-1)
    ,readable(reg.readable)
    ,writable(reg.writable)
    ,storage(1u<<reg.addr_width, 0u)
{
    if(base > 0xffffffff - (storage.size() - 1u))
        throw std::runtime_error(SB()<<name<<" has inconsistent base "<<base<<" and addr_width "<<reg.addr_width);
}

Simulator::Simulator(const osiSockAddr& ep, const JBlob& blob, const values_t &initial)
    :serveaddr(ep)
{
    for(JBlob::const_iterator it=blob.begin(), end=blob.end(); it!=end; ++it)
    {
        const JRegister& info = it->second;
        reg_by_name[info.name] = SimReg(info);
    }

    for(reg_by_name_t::iterator it=reg_by_name.begin(), end=reg_by_name.end(); it!=end; ++it)
    {
        SimReg& reg = it->second;
        for(epicsUInt32 addr = reg.base, endaddr = reg.base+reg.storage.size(); addr<endaddr; addr++)
        {
            reg_by_addr[addr] = &reg;
        }
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
                UnGuard U(G);

                fds[0].events = POLLIN;
                fds[1].events = POLLIN;
                fds[0].revents = fds[1].revents = 0; // paranoia

                int ret = ::poll(fds, 2, -1);
                if(ret<0)
                    throw SocketError(SOCKERRNO);

                if(fds[1].revents&POLLERR) {
                    throw std::runtime_error("socket error from wakeupRx");
                }

                if(fds[0].revents&POLLERR) {
                    throw std::runtime_error("socket error from serve");
                }

                if(fds[1].revents&POLLIN) {
                    char temp;
                    wakeupRx.recvall(&temp, 1);
                    stop = true;
                }


                if(fds[1].revents&POLLIN) {

                    buf.resize(pkt_size_limit);

                    serve.recvfrom(peer, buf);
                    process = true;
                }
            }
            // locked again

            if(process) {
                if(buf.size()%8) {
                    PrintAddr addr(peer);
                    errlogPrintf("%s: sent request with %u bytes of trailing junk\n",
                                 addr.c_str(), unsigned(buf.size()%8));
                }

                // build reply in place
                // leave header alone and start with first command at offset 8

                for(size_t i=8; i+8<buf.size(); i+=8) {
                    epicsUInt32 cmd_addr = ntohl(*reinterpret_cast<const char*>(&buf[i]));
                    epicsUInt32 data = ntohl(*reinterpret_cast<const char*>(&buf[i+4]));

                    reg_by_addr_t::iterator it(reg_by_addr.find(cmd_addr&0x00ffffff));

                    if(cmd_addr&0xef000000) {
                        PrintAddr addr(peer);
                        errlogPrintf("%s: unused bits set in cmd/address %08x\n", addr.c_str(), unsigned(cmd_addr));
                    }

                    if(it==reg_by_addr.end()) {
                        PrintAddr addr(peer);
                        errlogPrintf("%s: read of unknown cmd/address %08x\n", addr.c_str(), unsigned(cmd_addr));

                    } else {
                        SimReg& reg = *it->second;
                        size_t offset = (cmd_addr&0x00ffffff)-reg.base;

                        if(cmd_addr&0x10000000) {
                            // read
                            data = reg.storage[offset];

                        } else {
                            // write
                            reg.storage[offset] = data;
                        }
                    }

                    *reinterpret_cast<char*>(&buf[i+4]) = htonl(data);
                }

                UnGuard U(G);

                serve.sendto(peer, buf);
            }
        }

        running = false;
    }catch(...){
        running = false;
        throw;
    }
}

void Simulator::interrupt()
{
    char c = '!';

    wakeupTx.sendall(&c, 1);
}
