#include <stdexcept>
#include <fstream>
#include <sstream>

#include <vector>

#include <string.h>

#include <errlog.h>
#include <epicsUnitTest.h>
#include <testMain.h>

#include "utils.h"
#include "rom.h"

namespace {
// read entire file into vector
// not especially efficient
void readfile(std::vector<char>& out, const char *fname)
{
    std::string content(read_entire_file(fname));
    out.resize(content.size());

    std::copy(content.begin(),
              content.end(),
              out.begin());
}

void testDecode()
{
    testDiag("testDecode()");

    std::vector<char> romblob, json;

    readfile(romblob, "../rom.bin");
    readfile(json, "../jblob.json");

    testDiag("ROM size %u", unsigned(romblob.size()));

    ROM rom;

    rom.parse(&romblob[0], romblob.size());

    unsigned counts[4];
    memset(counts, 0, sizeof(counts));

    for(ROM::const_iterator it=rom.begin(), end=rom.end(); it!=end; ++it)
    {
        const ROMDescriptor& desc=*it;

        testDiag("ROMDescriptor type=%u size=%u (instance %u)",
                 unsigned(desc.type), unsigned(desc.value.size()), counts[desc.type]);

        switch(desc.type) {
        case ROMDescriptor::Invalid:
            testFail("Encountered Invalid header");
            break;
        case ROMDescriptor::Text:
            switch(counts[desc.type]) {
            case 0:
                testOk(desc.value=="LBNL LCLS-II LLRF Test stand support",
                       "Text value = \"%s\" (size %u)", desc.value.c_str(), unsigned(desc.value.size()));
                break;
            default:
                testFail("Unexpected Text descriptor");
            }

            break;
        case ROMDescriptor::BigInt:
            switch(counts[desc.type]) {
            case 0:
                testOk(desc.value=="576ca77f06293551faeb568e427d97045f37f2c6",
                       "BigInt0 value = \"%s\"", desc.value.c_str());
                break;
            case 1:
                testOk(desc.value=="0b7680e94bb69cd5f83e747d6542984071be5605",
                       "BigInt1 value = \"%s\"", desc.value.c_str());
                break;
            default:
                testFail("Unexpected BigInt descriptor");
            }
            break;
        case ROMDescriptor::JSON:
            switch(counts[desc.type]) {
            case 0:
                testOk1(desc.value+"\n"==std::string(&json[0], json.size()));
                break;
            default:
                testFail("Unexpected JSON descriptor");
            }
            break;
        }

        counts[desc.type]++;
    }

    testOk(counts[0]==0, "counts[0] = %u", counts[0]);
    testOk(counts[1]==1, "counts[1] = %u", counts[1]);
    testOk(counts[2]==2, "counts[2] = %u", counts[2]);
    testOk(counts[3]==1, "counts[3] = %u", counts[3]);
}

void testEncode()
{
    testDiag("testEncode()");

    ROM rom1;

    rom1.push_back(ROMDescriptor::Text, "Hello World"); // odd number of charactors
    rom1.push_back(ROMDescriptor::BigInt, "12345678abcdef");
    rom1.push_back(ROMDescriptor::BigInt, "12345678abcde"); // odd number of digits
    rom1.push_back(ROMDescriptor::JSON, "{}");

    std::vector<char> romblob(4096, 0u);

    romblob.resize(rom1.prepare(&romblob[0], romblob.size()));

    testOk(romblob.size()>4, "blob size=%u", unsigned(romblob.size()));

    ROM rom2;

    rom2.parse(&romblob[0], romblob.size());

    unsigned counts[4];
    memset(counts, 0, sizeof(counts));

    for(ROM::const_iterator it=rom2.begin(), end=rom2.end(); it!=end; ++it)
    {
        const ROMDescriptor& desc=*it;

        testDiag("ROMDescriptor type=%u size=%u (instance %u)",
                 unsigned(desc.type), unsigned(desc.value.size()), counts[desc.type]);

        switch(desc.type) {
        case ROMDescriptor::Invalid:
            testFail("Encountered Invalid header");
            break;
        case ROMDescriptor::Text:
            switch(counts[desc.type]) {
            case 0:
                testOk(desc.value=="Hello World",
                       "Text value = \"%s\" (size %u)", desc.value.c_str(), unsigned(desc.value.size()));
                break;
            default:
                testFail("Unexpected Text descriptor");
            }

            break;
        case ROMDescriptor::BigInt:
            switch(counts[desc.type]) {
            case 0:
                testOk(desc.value=="12345678abcdef00",
                       "BigInt0 value = \"%s\"", desc.value.c_str());
                break;
            case 1:
                testOk(desc.value=="12345678abcde000",
                       "BigInt1 value = \"%s\"", desc.value.c_str());
                break;
            default:
                testFail("Unexpected BigInt descriptor");
            }
            break;
        case ROMDescriptor::JSON:
            switch(counts[desc.type]) {
            case 0:
                testOk1(desc.value=="{}");
                break;
            default:
                testFail("Unexpected JSON descriptor");
            }
            break;
        }

        counts[desc.type]++;
    }

    testOk(counts[0]==0, "counts[0] = %u", counts[0]);
    testOk(counts[1]==1, "counts[1] = %u", counts[1]);
    testOk(counts[2]==2, "counts[2] = %u", counts[2]);
    testOk(counts[3]==1, "counts[3] = %u", counts[3]);
}

}

MAIN(testrom)
{
    testPlan(0);
    try {
        testDecode();
        testEncode();

        errlogFlush();
    }catch(std::exception& e){
        testAbort("Uncaught exception: %s", e.what());
    }
    return testDone();
}
