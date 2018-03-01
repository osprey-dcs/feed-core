
#include <stdexcept>
#include <string.h>

#include <epicsUnitTest.h>
#include <testMain.h>

#include <jblob.h>

#define testThrows(EXC, STMT) try{STMT; testFail("Statement failed to throw " #EXC " : " #STMT); }catch(std::exception& e) { testPass("Caught expected exception: %s", e.what());}

namespace {

void testEmpty()
{
    testDiag("testEmpty()");
    JBlob blob;

    blob.parse("{}", 2);
    testOk1(blob.registers.empty());
}

void testSyntaxError()
{
    testDiag("testSyntaxError()");
    JBlob blob;

    testThrows(std::runtime_error,  blob.parse("{foo");)

    testThrows(std::runtime_error, blob.parse("{");)

    testOk1(blob.registers.empty());
}

void testMyErrors()
{
    testDiag("testMyErrors()");
    JBlob blob;

    // too deep
    testThrows(std::runtime_error, blob.parse("{\"A\":{\"B\":{\"C\":4}}}");)

    // zero length key
    testThrows(std::runtime_error, blob.parse("{\"\":5}");)
}

void testAST()
{
    testDiag("testAST()");

    const char bigblob[] = "{"
                           "\"J18_debug\": {""\"access\": \"r\", \"addr_width\": 0, \"sign\": \"unsigned\", \"base_addr\": 63, \"data_width\": \"0x4\"},"
                           "\"__metadata__\": {\"slow_abi_ver\": 1}"
                           "}";

    JBlob blob;

    blob.parse(bigblob);

    JBlob::const_iterator it=blob.find("J18_debug");
    testOk1(it!=blob.end());
    if(it!=blob.end()) {
        const JRegister& reg = it->second;
        testOk(reg.base_addr==63, "base_addr %u", (unsigned)reg.base_addr);
        testOk(reg.addr_width==0, "addr_width %u", (unsigned)reg.addr_width);
        testOk(reg.data_width==4, "data_width %u", (unsigned)reg.data_width);
        testOk(reg.sign==JRegister::Unsigned, "sign %u", (unsigned)reg.sign);
        testOk1(reg.readable);
        testOk1(!reg.writable);

    } else {
        testSkip(6, "failed to find");
    }

    testOk(blob.info32["slow_abi_ver"]==1, "slow_abi_ver = %d", (int)blob.info32["slow_abi_ver"]);
}

}

MAIN(testjson)
{
    testPlan(14);
    try {
        testEmpty();
        testSyntaxError();
        testMyErrors();
        testAST();

    }catch(std::exception& e){
        testAbort("Uncaught exception: %s", e.what());
    }
    return testDone();
}
