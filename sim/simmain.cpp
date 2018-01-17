#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iostream>

#include <signal.h>

#include <epicsGetopt.h>

#include "jblob.h"
#include "rom.h"
#include "simulator.h"

namespace {

void usage(const char* exe)
{
    std::cout<<"Usage: "<<exe<<" [-hd] [-H <iface>[:<port>]] [-L none|rfs] <json_file> [initials_file]\n";
}

Simulator* volatile current;

void handler(int)
{
    if(current)
        current->interrupt();
}

} // namespace

int main(int argc, char *argv[])
{
    try {
        int opt;
        bool debug = false;
        std::string logic("none");
        osiSockAddr endpoint;
        memset(&endpoint, 0, sizeof(endpoint));
        endpoint.ia.sin_family = AF_INET;
        endpoint.ia.sin_addr.s_addr = htonl(INADDR_ANY);
        endpoint.ia.sin_port = htons(50006);

        while((opt=getopt(argc, argv, "hH:dL:"))!=-1) {
            switch(opt) {
            case 'H':
                if(aToIPAddr(optarg, 50006, &endpoint.ia))
                    throw std::runtime_error("-H with invalid host[:port]");
                break;
            case 'd':
                debug = true;
                break;
            case 'L':
                logic = optarg;
                break;
            default:
                std::cerr<<"Unknown option '"<<opt<<"'\n\n";
                // fall through
            case 'h':
                usage(argv[0]);
                return 2;
            }
        }

        if(optind>=argc) {
            std::cerr<<"JSON file not specified\n\n";
            usage(argv[0]);
            return 2;
        }

        // read input files

        std::string json(read_entire_file(argv[optind]));

        Simulator::values_t initial;
        if(optind+1<argc) {
            std::ifstream strm(argv[optind+1]);
            if(!strm.is_open()) {
                std::cerr<<"Failed to open '"<<argv[optind+1]<<"'\n";
                return 1;
            }

            std::string line;
            unsigned lineno=0;
            while(std::getline(strm, line)) {
                lineno++;
                epicsUInt32 addr, value;

                // skip blank
                if(line.find_first_not_of(" \t\r\n")==line.npos)
                    continue;

                std::istringstream lstrm(line);

                lstrm>>std::hex>>addr>>std::hex>>value;
                if(lstrm.bad() || !lstrm.eof())
                    throw std::runtime_error(SB()<<argv[optind+1]<<" Error on line "<<lineno);

                if(value!=0) {
                    initial[addr] = value;
                    if(debug)
                        std::cout<<"Initialize "<<std::hex<<addr<<" with "<<std::hex<<value<<"\n";
                }
            }
            if(strm.bad() || !strm.eof())
                throw std::runtime_error(SB()<<argv[optind+1]<<" Error on or after line "<<lineno<<" "<<std::hex<<strm.rdstate()<<"\n");
        }

        // build ROM image

        JBlob blob;
        blob.parse(json.c_str());

        ROM rom;
        rom.push_back(ROMDescriptor::Text, "FEED Simulator");
        rom.push_back(ROMDescriptor::BigInt, "0000000000000000000000000000000000000000");
        rom.push_back(ROMDescriptor::BigInt, "0000000000000000000000000000000000000000");
        rom.push_back(ROMDescriptor::JSON, json);

        feed::auto_ptr<Simulator> sim;
        if(logic=="none") {
            sim.reset(new Simulator(endpoint, blob, initial));
        } else if(logic=="rfs") {
            sim.reset(new Simulator_RFS(endpoint, blob, initial));
        } else {
            throw std::runtime_error(SB()<<"Unknown logic name: -L "<<logic);
        }
        sim->debug = debug;

        // copy in ROM image
        {
            SimReg& reg = (*sim)["ROM"];
            size_t len = rom.prepare(&reg.storage[0], reg.storage.size());
            std::cout<<"ROM contents "<<len<<"/"<<reg.storage.size()<<"\n";
        }

        if(debug) {
            std::cout<<"Registers:\n";
            for(Simulator::iterator it=sim->begin(), end=sim->end(); it!=end; ++it)
            {
                SimReg& reg = it->second;
                std::cout<<"  "<<reg.name<<"\t"
                         <<std::hex<<reg.base<<":"
                         <<std::hex<<(reg.base+reg.storage.size()-1)
                         <<"\n";
            }
        }

        signal(SIGINT, &handler);
        signal(SIGHUP, &handler);
        signal(SIGTERM, &handler);

        // time to run now...
        current = sim.get();
        sim->exec();
        current = 0;

        return 0;
    }catch(SocketError&e){
        if(e.code==SOCK_EINTR)
            return 0;
        std::cerr<<"Error: "<<e.what()<<"\n";
    }catch(std::exception& e){
        std::cerr<<"Error: "<<e.what()<<"\n";
        return 1;
    }
}
