
#include <fstream>
#include <sstream>
#include <iostream>

#include <epicsGetopt.h>

#include "jblob.h"
#include "simulator.h"

namespace {

void usage(const char* exe)
{
    std::cout<<"Usage: "<<exe<<" [-h] [-H <iface>[:<port>]] <json_file>\n";
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

        JBlob blob;
        blob.parse(argv[optind]);

        Simulator::values_t initial; // TODO

        Simulator sim(endpoint, blob, initial);

        sim.exec();

        return 0;
    }catch(std::exception& e){
        std::cerr<<"Error: "<<e.what()<<"\n";
        return 1;
    }
}
