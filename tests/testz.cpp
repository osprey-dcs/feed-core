#include <stdexcept>
#include <fstream>
#include <sstream>
#include <string.h>

#include <epicsUnitTest.h>
#include <testMain.h>

#include "utils.h"
#include "zpp.h"

#define testThrows(EXC, STMT) try{STMT; testFail("Statement failed to throw " #EXC " : " #STMT); }catch(std::exception& e) { testPass("Caught expected exception: %s", e.what());}

namespace {

// read entire file into vector
// not especially efficient
void readfile(std::vector<char>& out, const char *fname)
{
    std::ifstream fstrm(fname, std::ios_base::in|std::ios_base::binary);
    if(!fstrm.is_open())
        throw std::runtime_error(SB()<<"Failed to read test file "<<fname);
    std::ostringstream sstrm;

    sstrm << fstrm.rdbuf();

    std::string content(sstrm.str());
    out.resize(content.size());

    std::copy(content.begin(),
              content.end(),
              out.begin());
}

void testInflate()
{
    testDiag("testInflate()");

    // zlib.compress('')
    const char empty[] = "x\x9c\x03\x00\x00\x00\x00\x01";

    std::vector<char> txt;
    zinflate(txt, empty, sizeof(empty)-1);

    testOk(txt.size()==0, "expands to empty string size=%u", (unsigned)txt.size());

    txt.clear();

    // zlib.compress('Hello',9)
    const char hello[] = "x\xda\xf3H\xcd\xc9\xc9\x07\x00\x05\x8c\x01\xf5";

    zinflate(txt, hello, sizeof(hello)-1);
    txt.push_back('\0');
    testOk(strcmp(&txt[0], "Hello")==0, "%s == Hello", &txt[0]);
}

void testBig()
{
    testDiag("testBig()");

    std::vector<char> json, comp;
    readfile(json, "../jblob.json");
    readfile(comp, "../jblob.json.z");

    std::vector<char> actual;
    zinflate(actual, &comp[0], comp.size());

    testOk1(json==actual);

    // don't assume that all versions of zlib produce the same
    // compressed form.
    // so do a round-trip test for compression

    std::vector<char> comp2;

    zdeflate(comp2, &json[0], json.size());

    actual.clear();
    zinflate(actual, &comp2[0], comp2.size());

    testOk1(json==actual);
}
}

MAIN(testjson)
{
    testPlan(4);
    try {
        testInflate();
        testBig();

    }catch(std::exception& e){
        testAbort("Uncaught exception: %s", e.what());
    }
    return testDone();
}
