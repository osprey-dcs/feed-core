
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
    std::cout<<"Usage: "<<exe<<" [-h] [-H <iface>[:<port>]] <json_file>\n";
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
        osiSockAddr endpoint;
        memset(&endpoint, 0, sizeof(endpoint));
        endpoint.ia.sin_family = AF_INET;
        endpoint.ia.sin_addr.s_addr = htonl(INADDR_ANY);
        endpoint.ia.sin_port = htons(50006);

        while((opt=getopt(argc, argv, "hH:"))!=-1) {
            switch(opt) {
            case 'H':
                if(aToIPAddr(optarg, 50006, &endpoint.ia))
                    throw std::runtime_error("-H with invalid host[:port]");
                break;
            default:
                std::cerr<<"Unknown option '"<<opt<<"'\n\n";
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

        std::string json(read_entire_file(argv[optind]));

        Simulator::values_t initial; // TODO

        JBlob blob;
        blob.parse(json.c_str());

        ROM rom;
        rom.push_back(ROMDescriptor::Text, "FEED Simulator");
        rom.push_back(ROMDescriptor::BigInt, "0000000000000000000000000000000000000000");
        rom.push_back(ROMDescriptor::BigInt, "0000000000000000000000000000000000000000");
        rom.push_back(ROMDescriptor::JSON, json);

        Simulator sim(endpoint, blob, initial);

        {
            SimReg& reg = sim["ROM"];
            size_t len = rom.prepare(&reg.storage[0], reg.storage.size());
            std::cout<<"ROM contents "<<len<<"/"<<reg.storage.size()<<"\n";
        }

        signal(SIGINT, &handler);
        signal(SIGHUP, &handler);
        signal(SIGTERM, &handler);

        sim.exec();

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
