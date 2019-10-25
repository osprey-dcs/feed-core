
#include <iostream>
#include <stdexcept>
#include <string.h>

#include <errlog.h>
#include <dbUnitTest.h>
#include <dbAccess.h>
#include <dbChannel.h>
#include <testMain.h>
#include <aaiRecord.h>

#include <device.h>
#include <rom.h>
#include <simulator.h>

#ifndef TOP
// really defined in makefile.  stub here to help static analysis
#  define TOP ""
#endif

namespace {
struct simrunner : public epicsThreadRunable {
    osiSockAddr endpoint;
    PrintAddr addr;
    feed::auto_ptr<Simulator> instance;
    epicsThread runner;

    simrunner()
        :runner(*this, "FEEDSIM",
                epicsThreadGetStackSize(epicsThreadStackSmall),
                epicsThreadPriorityMedium)
    {
        endpoint.ia.sin_family = AF_INET;
        endpoint.ia.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        endpoint.ia.sin_port = htonl(0);
        // use well-known port to simply wireshark tracing
        //endpoint.ia.sin_port = htons(50006);

        JBlob blob;
        Simulator::values_t initial;

        std::string json(read_entire_file("../testdevice.json"));
        blob.parse(json.c_str());

        ROM rom;
        rom.push_back(ROMDescriptor::Text, "FEED Test");
        rom.push_back(ROMDescriptor::BigInt, "0000000000000000000000000000000000000000");
        rom.push_back(ROMDescriptor::BigInt, "0000000000000000000000000000000000000000");
        rom.push_back(ROMDescriptor::JSON, json);

        instance.reset(new Simulator(endpoint, blob, initial));

        // copy in ROM image
        {
            SimReg& reg = (*instance)["ROM"];
            size_t len = rom.prepare(&reg.storage[0], reg.storage.size());
            testDiag("ROM contents %zu/%zu",len,reg.storage.size());
        }

        instance->endpoint(endpoint);
        addr = endpoint;
        testDiag("Simulator running @%s", addr.c_str());

        runner.start();
    }

    ~simrunner()
    {
        testDiag("Simulator stopping");
        instance->interrupt();
        runner.exitWait();
        testDiag("Simulator stopped");
    }

    virtual void run()
    {
        instance->exec();
    }
};

struct Channel {
    dbChannel *chan;
    Channel(const char *name)
        :chan(dbChannelCreate(name))
    {
        if(!chan)
            throw std::runtime_error(SB()<<"Failed to create channel to "<<name);
        if(dbChannelOpen(chan))
            throw std::runtime_error(SB()<<"Failed to open channel to "<<name);
    }
    ~Channel() {
        dbChannelDelete(chan);
    }
    epicsEnum16 getEnum() {
        epicsEnum16 ret;
        if(dbChannelGet(chan, DBF_ENUM, &ret, 0, 0, 0))
            throw std::runtime_error("get failed");
        return ret;
    }
};
}

extern "C" {
void testfeed_registerRecordDeviceDriver(struct dbBase *);
}

MAIN(testdevice)
{
    testPlan(7);
    try {
        simrunner sim;

        testdbPrepare();

        testdbReadDatabase("testfeed.dbd", 0, 0);
        testfeed_registerRecordDeviceDriver(pdbbase);

        testdbReadDatabase("feed_base.template", ".:" TOP "/db", "PREF=tst:,NAME=test");
        testdbReadDatabase("testdevice.db", ".:..:", "PREF=tst:,NAME=test");

        eltc(0);
        testIocInitOk();
        eltc(1);

        Device *dev;
        {
            Device::devices_t::iterator it(Device::devices.find("test"));
            if(it==Device::devices.end())
                testAbort("Device not created");
            dev = it->second;
        }
        (void)dev;

        testdbPutFieldOk("tst:IPADDR", DBF_STRING, sim.addr.c_str());

        {
            Channel state("tst:STATUS");
            unsigned N;
            epicsEnum16 S;
            for(N=5; N && (S=state.getEnum())<4 ; N--)
                epicsThreadSleep(1.0);

            testOk(N>0, "timeout count %u", N);
            testOk(S==4, "Final state %u", S);
        }

        testdbPutFieldOk("tst:HelloInt-I.PROC", DBF_LONG, 1);
        testdbGetFieldEqual("tst:HelloInt-I", DBF_LONG, 0x48656c6c);

        testdbPutFieldOk("tst:One-SP", DBF_LONG, 0x12345678);
        while(1) {
            dbCommon *prec = testdbRecordPtr("tst:One-SP");
            int pact;
            dbScanLock(prec);
            pact = prec->pact;
            dbScanUnlock(prec);
            if(!pact)
                break;
        }

        testOk((*sim.instance)["one"].storage[0]==0x12345678,
                "one[0] == %08x", (unsigned)(*sim.instance)["one"].storage[0]);

        //dev->show(std::cerr);

        testIocShutdownOk();

        testdbCleanup();
        errlogFlush();
    }catch(std::exception& e){
        errlogFlush();
        testAbort("Unhandled exception: %s", e.what());
    }
    return testDone();
}
