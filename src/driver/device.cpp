
#include <iostream>
#include <algorithm>

#include <poll.h>

#include <errlog.h>
#include <alarm.h>
#include <dbAccess.h>
#include <recSup.h>
#include <epicsExit.h>

#include "device.h"
#include "rom.h"

// max number of concurrent requests
int feedNumInFlight = 1;
// timeout for reply (in sec.)
double feedTimeout = 1.0;

namespace {
const size_t pkt_size_limit = 128*8;

// description of automatic/bootstrap registers
const struct gblrom_t {
    JRegister jrom_info;
    JRegister jid_info;
    gblrom_t() {
        jid_info.name = "HELLO";
        jid_info.description = "Hello World!";
        jid_info.addr_width = 2; // 4 words
        jid_info.base_addr = 0;
        jid_info.data_width = 32;
        jid_info.readable = true;

        jrom_info.name = "ROM";
        jrom_info.description = "Static configuration";
        jrom_info.base_addr = 0x800;
        jrom_info.addr_width = 11; // 2048 words
        jrom_info.data_width = 16;
        jrom_info.readable = true;
    }
} gblrom;
}

RegInterest::RegInterest()
    :reg(0)
{
    scanIoInit(&changed);
}

#define IFDBG(N, FMT, ...) if(dev->debug&(1u<<(N))) errlogPrintf("%s %s : " FMT "\n", logTime(), dev->myname.c_str(), __VA_ARGS__)

DevReg::DevReg(Device *dev, const JRegister &info, bool bootstrap)
    :dev(dev)
    ,info(info)
    ,bootstrap(bootstrap)
    ,state(Invalid)
    ,mem(1u<<info.addr_width, 0)
    ,received(1u<<info.addr_width, false)
    ,nremaining(0u)
    ,next_send(mem.size())
{}

void DevReg::process()
{
    dev->records.splice(dev->records.end(),
                        records);
}

void DevReg::scan_interested()
{
    for(interested_t::const_iterator it = interested.begin(),
        end = interested.end();
        it != end; ++it)
    {
        assert((*it)->reg = this);

        scanIoRequest((*it)->changed);
    }
}

bool DevReg::queue(bool write)
{
    if(inprogress())
        return false;

    std::fill(received.begin(),
              received.end(),
              false);
    nremaining = received.size();
    next_send = 0;

    if((!write && !info.readable)
            || (write && !info.writable))
        throw std::runtime_error("Register does not support requested operation");

    dev->reg_send.push_back(this);

    state = write ? Writing : Reading;
    IFDBG(1, "queue %s for %s from %06x",
                 info.name.c_str(),
                 write ? "write" : "read",
                 (unsigned)next_send);

    dev->poke_runner();
    return true;
}

#undef IFDBG

Device::devices_t Device::devices;

const char *Device::current_name[5] = {
    "Error",
    "Idle",
    "Searching",
    "Inspecting",
    "Running",
};

static void feed_shutdown(void *raw)
{
    Device *dev = (Device*)raw;

    {
        Guard G(dev->lock);
        dev->runner_stop = true;
    }
    dev->poke_runner();
    dev->runner.exitWait();
}

#define IFDBG(N, FMT, ...) if(debug&(1u<<(N))) errlogPrintf("%s %s : " FMT "\n", logTime(), myname.c_str(), ##__VA_ARGS__)

Device::Device(const std::__cxx11::string &name, osiSockAddr &ep)
    :sock(AF_INET, SOCK_DGRAM, 0)
    ,myname(name)
    ,debug(0xffffffff)
    ,current(Idle)
    ,cnt_sent(0u)
    ,cnt_recv(0u)
    ,cnt_ignore(0u)
    ,cnt_timo(0u)
    ,cnt_err(0u)
    ,reg_rom(new DevReg(this, gblrom.jrom_info, true))
    ,reg_id(new DevReg(this, gblrom.jid_info, true))
    ,inflight(feedNumInFlight)
    ,want_to_send(false)
    ,runner_stop(false)
    ,reset_requested(false)
    ,runner(*this,
            "FEED",
            epicsThreadGetStackSize(epicsThreadStackSmall),
            epicsThreadPriorityHigh)
{
    memset(&peer_addr, 0, sizeof(peer_addr));

    epicsTimeStamp now;
    epicsTimeGetCurrent(&now);
    // pseudo-random initial sequence number
    send_seq = now.nsec;

    scanIoInit(&current_changed);
    last_message = "Startup";

    sock.set_blocking(false);
    sock.bind(ep);

    Socket::pipe(wakeupRx, wakeupTx);

    // make non-blocking so we can safely poke_runner() w/ lock held
    wakeupTx.set_blocking(false);

    reset();

    epicsAtExit(feed_shutdown, this);
}

Device::~Device()
{
    // never free'd
    assert(false);
}

void Device::poke_runner()
{
    wakeupTx.trysend("!", 1);
}

void Device::request_reset()
{
    reset_requested = true;
    poke_runner();
}

void Device::reset()
{
    IFDBG(0, "reset w/ %s", peer_name.c_str());

    want_to_send = false;
    reset_requested = false;
    current = Idle;

    // forget about pending or Sent messages
    for(size_t i=0, N=inflight.size(); i<N; i++)
    {
        inflight[i].clear();
    }

    reg_send.clear();

    for(reg_by_name_t::const_iterator it = reg_by_name.begin(), end = reg_by_name.end();
        it != end; ++it)
    {
        DevReg *reg = it->second;

        // complete any in-progress async
        reg->process();

        if(!reg->bootstrap) {
            delete reg;
        } else {
            reg->state = DevReg::Invalid;
        }
    }

    // restart with only automatic/bootstrap registers
    reg_by_name.clear();
    reg_by_name["HELLO"] = reg_id.get();
    reg_by_name["ROM"] = reg_rom.get();

    last_message.clear();

    for(reg_interested_t::const_iterator it = reg_interested.begin(), end = reg_interested.end();
        it != end; ++it)
    {
        if(it->second->reg && it->second->reg->bootstrap)
            continue;

        // break association with register
        it->second->reg = 0;
        scanIoRequest(it->second->changed);
    }

    scanIoRequest(current_changed);
}

void Device::handle_send(Guard& G)
{
    const epicsTime now(epicsTime::getCurrent()),
                    due(now + feedTimeout);

    // first pass to populate DevMsg
    for(size_t i=0, N=inflight.size(); i<N && !reg_send.empty(); i++)
    {
        DevMsg& msg = inflight[i];
        if(msg.state==DevMsg::Sent)
            continue;
        // found available message slot

        DevReg *R = reg_send.front();

        assert(R->inprogress());
        assert(R->next_send < R->mem.size() );

        for(unsigned j=0; j<DevMsg::nreg && R->next_send < R->mem.size(); j++) {
            if(msg.reg[j])
                continue;
            // found available address slot in message

            msg.buf.resize(2*j + 4);

            epicsUInt32 offset = R->next_send++;
            epicsUInt32 addr = R->info.base_addr + offset;
            epicsUInt32 val = 0;

            if(R->state==DevReg::Reading) {
                addr |= 0x10000000;

            } else {
                val = R->mem.at(offset);
            }

            msg.reg[j] = R;
            msg.buf[2*j + 2] = htonl(addr);
            msg.buf[2*j + 3] = val;
        }

        if(R->next_send>=R->mem.size()) {
            // all addresses of this register have been sent
            reg_send.pop_front();
        }

        if(msg.reg[0]) {
            msg.state = DevMsg::Ready;
        }
    }

    // second pass to send any Ready
    unsigned nsent=0;
    for(size_t i=0, N=inflight.size(); i<N; i++)
    {
        DevMsg& msg = inflight[i];
        if(msg.state!=DevMsg::Ready)
            continue;

        while(msg.buf.size()<8) {
            // pad with reads of offset zero ("Hell" register)
            msg.buf.push_back(htonl(0x10000000));
            msg.buf.push_back(0);
        }

        msg.seq = (i<<24) | ((send_seq++)&0x00ffffff);

        msg.buf[0] = htonl(0xfeedc0de);
        msg.buf[1] = htonl(msg.seq);

        try {
            // non-blocking sendto(), so we don't unlock here
            sock.sendto(peer_addr, (const char*)&msg.buf[0], msg.buf.size()*4);
            cnt_sent++;
        }catch(SocketBusy&){
            want_to_send = true;
            return;
        }
        IFDBG(1, "Send seq=%08x %zu bytes", (unsigned)msg.seq, msg.buf.size()*4u);

        msg.due = due;
        msg.state = DevMsg::Sent;
        nsent++;
    }

    IFDBG(4, "Sent %u messages", nsent);
}

void Device::handle_process(const std::vector<char>& buf, PrintAddr& addr)
{
    if(buf.size()<32) {
        cnt_ignore++;
        IFDBG(0, "Ignore short %zu byte message from %s", buf.size(), addr.c_str());
        return;
    }

    const epicsUInt32 *ibuf = (const epicsUInt32*)&buf[0];
    const size_t ilen = buf.size()/4;

    epicsUInt32 seq = ntohl(ibuf[1]);
    epicsUInt8 off = seq>>24;

    IFDBG(1, "Recv seq=%08x %zu bytes", (unsigned)seq, buf.size());

    if(ntohl(ibuf[0])!=0xfeedc0de || off>=inflight.size()) {
        cnt_ignore++;
        IFDBG(0, "Ignore corrupt message from %s (%08x %08x)", addr.c_str(),
                     (unsigned)ibuf[0], (unsigned)ibuf[1]);
        return;

    } else if(inflight[off].state!=DevMsg::Sent) {
        IFDBG(0, "Duplicate ignored seq=%08x", (unsigned)seq);
        cnt_ignore++;
        return;

    } else if(inflight[off].seq != seq) {
        cnt_ignore++;
        IFDBG(0, "Ignore stale/duplicate message from %s (%08x)", addr.c_str(), (unsigned)ibuf[0]);
        return;
    }

    DevMsg& msg = inflight[off];

    for(unsigned j=0; j<DevMsg::nreg; j++) {
        if(!msg.reg[j] || !msg.reg[j]->inprogress())
            continue;

        if(j*2+3 >= ilen) {
            IFDBG(0, "Warning: reply truncated");
            continue;

        }else if((ibuf[j*2 + 2]&0xfffffff0)!=msg.buf[j*2+2]) {
            // we don't expect re-ordering or change in command type
            // but allow 4 bits don't-care as future status bits.
            IFDBG(0, "Warning: response type doesn't match request (%08x %08x)",
                         (unsigned)ibuf[j*2 + 2], (unsigned)msg.buf[j*2+2]);
            continue;
        }

        if(msg.reg[j]) {
            epicsUInt32 cmd_addr = ntohl(ibuf[j*2 + 2]),
                        data     = ibuf[j*2 + 3];

            epicsUInt32 offset = (cmd_addr&0x00ffffff) - msg.reg[j]->info.base_addr;

            if(cmd_addr&0x10000000) {
                msg.reg[j]->mem.at(offset) = data;
            } else {
                // writes simply echo written value, ignore data
            }

            if(!msg.reg[j]->received.at(offset)) {
                msg.reg[j]->received.at(offset) = true;
                msg.reg[j]->nremaining--;
            }

            if(!msg.reg[j]->nremaining)
            {
                assert(std::find(msg.reg[j]->received.begin(),
                                 msg.reg[j]->received.end(),
                                 false) == msg.reg[j]->received.end());

                // all addresses received
                msg.reg[j]->state = DevReg::InSync;

                msg.reg[j]->stat = 0;
                msg.reg[j]->sevr = 0;
                msg.reg[j]->process();

                msg.reg[j]->scan_interested();
                IFDBG(1, "complete %s", msg.reg[j]->info.name.c_str());
            }
        }


    }

    msg.clear();
}

void Device::handle_timeout()
{
    epicsTime now(epicsTime::getCurrent());

    for(size_t i=0, N=inflight.size(); i<N; i++)
    {
        DevMsg& msg = inflight[i];
        if(msg.state!=DevMsg::Sent || msg.due>now)
            continue;

        IFDBG(1, "timeout seq=%08x", (unsigned)msg.seq);

        // timeout!
        cnt_timo++;

        // Full reset following any timeout
        reset_requested = true;

        for(unsigned j=0; j<DevMsg::nreg; j++)
        {
            if(!msg.reg[j])
                continue;
            DevReg *reg = msg.reg[j];

            assert(reg->inprogress());

            IFDBG(1, "timeout for register %s", msg.reg[j]->info.name.c_str());

            // orphan any replies for this register
            for(size_t m=0; m<N; m++)
            {
                for(unsigned k=0; k<DevMsg::nreg; k++)
                {
                    inflight[m].reg[k] = 0;
                }
            }

            if(!reg_send.empty() && reg_send.front()==reg) {
                reg_send.pop_front();
            } else {
                assert(reg->next_send>=reg->mem.size());
            }

            reg->state = DevReg::Invalid;

            reg->stat = COMM_ALARM;
            reg->sevr = INVALID_ALARM;
            reg->process();

            reg->scan_interested();
        }

        msg.clear();
    }
}

void Device::handle_inspect()
{
    ROM rom;

    rom.parse((char*)&reg_rom->mem[0], reg_rom->mem.size()*4);
    std::string json;

    unsigned i=0;
    for(ROM::infos_t::const_iterator it = rom.begin(), end = rom.end();
        it != end; ++it, i++)
    {
        const ROMDescriptor& desc = *it;

        switch(desc.type) {
        case ROMDescriptor::Invalid:
            IFDBG(2, "ROM contains invalid descriptor #%u", i);
            break;
        case ROMDescriptor::Text:
            IFDBG(2, "ROM desc \"%s\"", desc.value.c_str());
            break;
        case ROMDescriptor::BigInt:
            break; // ignore
        case ROMDescriptor::JSON:
            if(json.empty())
                json = desc.value;
            else
                IFDBG(2, "ROM ignoring addition JSON #%u", i);

            break;
        }
    }

    if(json.empty())
        throw std::runtime_error("ROM contains no JSON");

    JBlob blob;
    blob.parse(json.c_str());

    for(JBlob::const_iterator it = blob.begin(), end = blob.end(); it != end; ++it)
    {
        const JRegister& reg = it->second;

        if(reg_by_name.find(reg.name)!=reg_by_name.end())
            continue; // don't overwrite automatic/bootstrap register

        IFDBG(2, "add register %s", reg.name.c_str());

        feed::auto_ptr<DevReg> dreg(new DevReg(this, reg));
        // The following isn't exception safe (leaves dangling pointers)
        // however, the catch in run() will call reset() which cleans these up

        // fill in list of interested records
        std::pair<reg_interested_t::const_iterator, reg_interested_t::const_iterator> range;
        range = reg_interested.equal_range(reg.name);

        for(; range.first != range.second; ++range.first)
        {
            RegInterest *interest = range.first->second;
            dreg->interested.push_back(interest);
            interest->reg = dreg.get();
        }

        reg_by_name[reg.name] = dreg.release();
    }
}

void Device::handle_state()
{
    state_t prev = current;
    if(reset_requested) {
        reset();
        return;
    }

    switch(current) {
    case Error:
        break; //TODO: remove for auto-recover?
    case Idle:
        if(!peer_name.empty()) {
            current = Searching;
            IFDBG(3, "Searching for %s", peer_name.c_str());
        }
        break;
    case Searching:
        if(reg_id->state==DevReg::InSync) {
            // InSync means reply received
            reg_rom->queue(false);
            current = Inspecting;

        }else if(!reg_id->inprogress()) {
            // request again
            reg_id->queue(false);
        }
        break;
    case Inspecting:
        if(reg_rom->state==DevReg::InSync) {
            handle_inspect();
            current = Running;
        }
        break;
    case Running:
        break;
    }
    if(prev!=current)
        IFDBG(3, "Transition %s -> %s", current_name[prev], current_name[current]);
}

void Device::run()
{
    IFDBG(4, "Worker starts");
    Guard G(lock);

    std::vector<std::vector<char> > bufs(inflight.size());
    // pre-alloc Rx buffers
    for(size_t i=0; i<bufs.size(); i++)
        bufs[i].resize(pkt_size_limit+16);

    std::vector<bool> doProcess(inflight.size(), false);

    std::vector<PrintAddr> addrs(inflight.size());

    pollfd fds[2];
    fds[0].fd = sock;
    fds[1].fd = wakeupRx;

    while(!runner_stop) {
        try {
            IFDBG(4, "Looping state=%u", current);

            if(active())
                handle_send(G);

            fds[0].events = POLLIN;
            fds[1].events = POLLIN;
            fds[0].revents = fds[1].revents = 0; // paranoia

            if(want_to_send) {
                fds[0].events |= POLLOUT;
                want_to_send = false;
            }

            {
                DevReg::records_t completed;
                completed.swap(records);

                UnGuard U(G);

                for(DevReg::records_t::const_iterator it = completed.begin(), end = completed.end();
                    it != end; ++it)
                {
                    (*it)->complete();
                }

                std::fill(doProcess.begin(),
                          doProcess.end(),
                          false);

                int ret = ::poll(fds, 2, feedTimeout*1000);
                if(ret<0) {
                    throw SocketError(SOCKERRNO);

                } else if(ret==0) {
                    // timeout w/o reply

                } else {

                    if(fds[1].revents&(POLLERR|POLLHUP)) {
                        throw std::runtime_error("socket error from wakeupRx");
                    }

                    if(fds[0].revents&POLLERR) {
                        throw std::runtime_error("socket error from serve");
                    }

                    if(fds[1].revents&POLLIN) {
                        fds[1].revents &= ~POLLIN;

                        char temp[16];
                        wakeupRx.recvall(temp, 16);
                    }

                    if(fds[0].revents&POLLIN) {
                        fds[0].revents &= ~POLLIN;

                        unsigned nrecv=0;
                        for(size_t i=0, N=bufs.size(); i<N; i++)
                        {

                            bufs[i].resize(pkt_size_limit+16);

                            osiSockAddr peer;
                            try{
                                sock.recvfrom(peer, bufs[i]);
                                addrs[i] = peer;
                                doProcess[i] = true;
                                cnt_recv++;
                            }catch(SocketBusy&){
                                break;
                            }

                            if(!doProcess[i]) {
                                // nothing to process
                            } else if(!sockAddrAreIdentical(&peer, &peer_addr)) {
                                IFDBG(4, "Warning, RX ignore message from %s", addrs[i].c_str());
                                cnt_ignore++;
                                doProcess[i] = false;

                            } else if(bufs[i].size()>pkt_size_limit+7) {
                                // complain if an extra cmd+addr+data is included
                                IFDBG(0, "Warning, RX message truncated expect=%zu threshold=%zu",
                                      bufs[i].size(), pkt_size_limit+7);
                            }

                            if(doProcess[i])
                                nrecv++;
                        }

                        IFDBG(4, "Received %u messages", nrecv);
                    }

                    if(fds[0].revents&POLLOUT) {
                        fds[0].revents &= ~POLLOUT;
                        // loop around and try to send
                    }

                    if(fds[0].revents || fds[1].revents)
                        IFDBG(4, "Unhandled poll() events [0]=%x [1]=%x", fds[0].revents, fds[1].revents);
                }

                // re-lock
            }

            for(size_t i=0, N=bufs.size(); i<N; i++)
            {
                if(doProcess[i]) {
                    handle_process(bufs[i], addrs[i]);
                }
            }

            handle_timeout();

            handle_state();

            scanIoRequest(current_changed);

        } catch(std::exception& e) {
            cnt_err++;
            reset();
            current = Error;
            errlogPrintf("%s: exception in worker: %s\n", myname.c_str(), e.what());
            last_message = e.what();
            scanIoRequest(current_changed);
        }
    }
    IFDBG(4, "Runner stopping");
}


void Device::show(std::ostream& strm, int lvl) const
{
    Guard G(lock);

    strm<<" Current state: "<<current_name[current]<<"\n"
          " Peer: "<<peer_name<<"\n"
          " Msg: "<<last_message<<"\n"
          " Cnt Tx: "<<cnt_sent<<"\n"
          " Cnt Rx: "<<cnt_recv<<"\n"
          " Cnt Ig: "<<cnt_ignore<<"\n"
          " Cnt TM: "<<cnt_timo<<"\n"
          " Cnt ER: "<<cnt_err<<"\n"
          " Cnt SQ: "<<send_seq<<"\n"
          ;

    if(lvl<=0)
        return;

    strm<<" Messages:\n";
    for(size_t i=0; i<inflight.size(); i++)
    {
        strm<<"  ["<<i<<"] -> ";
        inflight[i].show(strm, lvl);
    }

    if(lvl<=1)
        return;

    strm<<" Send queue:\n";
    for(reg_send_t::const_iterator it(reg_send.begin()), end(reg_send.end());
        it != end; ++it)
    {
        const DevReg *reg = *it;
        strm<<"  "<<reg->info.name<<"\n";
    }

    if(lvl<=2)
        return;

    strm<<" Interests:\n";
    for(reg_interested_t::const_iterator it = reg_interested.begin(), end = reg_interested.end();
        it != end; ++it)
    {
        strm<<"  ";
        it->second->show(strm, lvl);
        strm<<" -> "<<it->first<<"\n";
    }

    if(lvl<=3)
        return;

    strm<<" Registers:\n";

    for(reg_by_name_t::const_iterator it = reg_by_name.begin(), end = reg_by_name.end();
        it != end; ++it)
    {
        strm<<"  "<<it->first;
        it->second->show(strm, lvl);
    }
}


void DevMsg::show(std::ostream& strm, int lvl) const
{
    switch(state) {
    case Free:
        strm<<"Free\n";
        return;
    case Ready:
        strm<<"Ready\n";
        break;
    case Sent:
        strm<<"Sent due=";
    {
        char buf[128];
        due.strftime(buf, sizeof(buf), "%H:%M:%S.%f");
        buf[sizeof(buf)-1] = '\0';
        strm<<buf<<"\n";
    }
        break;
    }
}


void DevReg::show(std::ostream& strm, int lvl) const
{
    if(lvl>=2)
        strm<<" : "<<this->info;

    strm<<"\n"
          "   Reg State: "<<this->state<<"\n"
          "   next_send: "<<this->next_send<<"\n"
          "   Pending async records:\n"
          ;

    for(DevReg::records_t::const_iterator it2 = this->records.begin(), end2 = this->records.end();
        it2 != end2; ++it2)
    {
        RegInterest * const reg = *it2;
        strm<<"    ";
        reg->show(strm, lvl);
        strm<<"\n";
    }

    strm<<"  Interested records:\n";
    for(DevReg::interested_t::const_iterator it2(this->interested.begin()), end2(this->interested.end());
        it2 != end2; ++it2)
    {
        RegInterest *reg(*it2);
        strm<<"    ";
        reg->show(strm, lvl);
        strm<<"\n";
    }
}
