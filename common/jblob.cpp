
#include <stdexcept>
#include <list>
#include <string.h>

#include <errlog.h>
#include <yajl_parse.h>

#include "utils.h"
#include "jblob.h"

namespace {

// undef implies API version 0
#ifndef EPICS_YAJL_API_VERSION
typedef long integer_arg;
typedef unsigned size_arg;
#else
typedef long long integer_arg;
typedef size_t size_arg;
#endif

struct context {
    JBlob blob;

    std::string err, // error message
                regname,
                param;
    unsigned depth;

    JRegister scratch;

    typedef std::list<std::string> warnings_t;
    warnings_t warnings;

    void warn(const std::string& msg) {
        warnings.push_back(msg);
    }

    context() :depth(0) {}
};

#define TRY context *self = (context*)ctx; try

#define CATCH() catch(std::exception& e) { if(self->err.empty()) self->err = e.what(); return 0; }

int jblob_null(void *ctx)
{
    TRY {
        self->warn("ignoring unsupported: null");
    }CATCH()
    return 1;
}

int jblob_boolean(void *ctx, int val)
{
    TRY {
        self->warn(SB()<<"ignoring unsupported: boolean value "<<val);
    }CATCH()
    return 1;
}

int jblob_integer(void *ctx, integer_arg val)
{
    TRY {
        if(self->depth==2) {
            if(self->param=="base_addr") {
                self->scratch.base_addr = val;

            } else if(self->param=="addr_width") {
                self->scratch.addr_width = val;

            } else if(self->param=="data_width") {
                self->scratch.data_width = val;

            } else {
                self->warn(SB()<<self->regname<<"."<<self->param<<" ignores integer value");
            }
        } else
            self->warn(SB()<<"ignored integer value at depth="<<self->depth);
        return 1;
    }CATCH()
}

int jblob_double(void *ctx, double val)
{
    TRY {
        self->warn(SB()<<"ignoring unsupported: double value "<<val);
    }CATCH()
    return 1;
}

int jblob_string(void *ctx, const unsigned char *val, size_arg len)
{
    TRY {
        std::string V((const char*)val, len);

        if(self->depth==2) {
            if(self->param=="access") {
                self->scratch.readable = V.find_first_of('r')!=V.npos;
                self->scratch.writable = V.find_first_of('w')!=V.npos;

            } else if(self->param=="description") {
                self->scratch.description = V;

            } else if(self->param=="sign") {
                if(V=="unsigned") {
                    self->scratch.sign = JRegister::Unsigned;

                } else if(V=="signed") {
                    self->scratch.sign = JRegister::Signed;

                } else {
                    self->warn(SB()<<self->regname<<"."<<self->param<<" unknown value "<<V<<" assume Unsigned");
                }

            } else {
                self->warn(SB()<<self->regname<<"."<<self->param<<" ignores string value");
            }
        } else
            self->warn(SB()<<"ignored string value at depth="<<self->depth);
        return 1;
    }CATCH()
}

int jblob_start_map(void *ctx)
{
    TRY {
        self->depth++;
        if(self->depth>2) {
            throw std::runtime_error("Object depth limit (2) exceeded");
        }
        return 1;
    }CATCH()
}

int jblob_map_key(void *ctx, const unsigned char *key, size_arg len)
{
    TRY {
        if(len==0)
            throw std::runtime_error("Zero length key not allowed");
        std::string K((const char*)key, len);
        if(self->depth==1) {
            self->regname = K;
            if(self->blob.registers.find(K)!=self->blob.registers.end()) {
                self->warn(SB()<<"Duplicate definition for register "<<self->regname);
            }
        } else if(self->depth==2) {
            self->param = K;
        } else {
            throw std::logic_error("key at unsupported depth");
        }
        return 1;
    }CATCH()
}

int jblob_end_map(void *ctx)
{
    TRY {
        if(self->depth==2) {
            self->scratch.name = self->regname;
            self->blob.registers[self->regname] = self->scratch;
            self->scratch.clear();
        }
        if(self->depth==0)
            throw std::logic_error("Object depth underflow");
        self->depth--;
        return 1;
    }CATCH()
}

yajl_callbacks jblob_cbs = {
    &jblob_null, // null
    &jblob_boolean, // boolean
    &jblob_integer,
    &jblob_double, // double
    NULL, // number
    &jblob_string,
    &jblob_start_map,
    &jblob_map_key,
    &jblob_end_map,
    NULL, // start array
    NULL, // end array
};

struct handler {
    yajl_handle handle;
    handler(yajl_handle handle) :handle(handle)
    {
        if(!handle)
            throw std::runtime_error("Failed to allocate yajl handle");
    }
    ~handler() {
        yajl_free(handle);
    }
    operator yajl_handle() { return handle; }
};

} // namespace

void JBlob::parse(const char *buf)
{
    parse(buf, strlen(buf));
}

void JBlob::parse(const char *buf, size_t buflen)
{
#ifndef EPICS_YAJL_API_VERSION
    yajl_parser_config conf;
    memset(&conf, 0, sizeof(conf));
    conf.allowComments = 1;
    conf.checkUTF8 = 1;
#endif

    context ctxt;

#ifndef EPICS_YAJL_API_VERSION
    handler handle(yajl_alloc(&jblob_cbs, &conf, NULL, &ctxt));
#else
    handler handle(yajl_alloc(&jblob_cbs, NULL, &ctxt));
#endif

    yajl_status sts = yajl_parse(handle, (const unsigned char*)buf, buflen);
#ifndef EPICS_YAJL_API_VERSION
    if(sts==yajl_status_insufficient_data) {
        sts = yajl_parse_complete(handle);
    }
#else
    if(sts==yajl_status_ok)
        sts = yajl_complete_parse(handle);
#endif
    switch(sts) {
    case yajl_status_ok:
        break;
    case yajl_status_error: {
        std::string msg;
        unsigned char *raw = yajl_get_error(handle, 1, (const unsigned char*)buf, buflen);
        try {
            msg = (char*)raw;
            yajl_free_error(handle, raw);
        }catch(...){
            yajl_free_error(handle, raw);
            throw;
        }
        throw std::runtime_error(msg);
    }
    case yajl_status_client_canceled:
        throw std::runtime_error(ctxt.err);
#ifndef EPICS_YAJL_API_VERSION
    case yajl_status_insufficient_data:
        throw std::runtime_error("Unexpected end of input");
#endif
    }

    for(context::warnings_t::const_iterator it=ctxt.warnings.begin(), end=ctxt.warnings.end();
        it!=end; ++it)
    {
        errlogPrintf("JSON parser warning: %s\n", it->c_str());
    }

    registers.swap(ctxt.blob.registers);
}

std::ostream& operator<<(std::ostream& strm, const JBlob& blob)
{
    strm<<"{";
    bool first=true;
    for(JBlob::registers_t::const_iterator it=blob.registers.begin(), end=blob.registers.end();
        it!=end; ++it)
    {
        if(first) first=false;
        else strm<<", ";
        strm<<"\""<<it->first<<"\":"<<it->second;
    }
    strm<<"}";
    return strm;
}

std::ostream& operator<<(std::ostream& strm, const JRegister& reg)
{
    strm<<"{"
          "\"access\":\""<<(reg.readable?"r":"")<<(reg.writable?"w":"")<<"\", "
          "\"addr_width\":"<<reg.addr_width<<", "
          "\"base_addr\":"<<reg.base_addr<<", "
          "\"data_width\":"<<reg.data_width<<", "
          "\"description\":\""<<reg.description<<"\", "
          "\"sign\":\""<<(reg.sign==JRegister::Unsigned?"un":"")<<"signed\""
          "}";
    return strm;
}
