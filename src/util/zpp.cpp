
#include <stdexcept>
#include <string.h>

#include <zlib.h>

#include "utils.h"
#include "zpp.h"

void zdeflate(std::vector<char>& out, const char *in, size_t inlen, int lvl)
{
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    int err = deflateInit(&strm, lvl);
    if(err!=Z_OK)
        throw std::runtime_error(SB()<<"deflateInit() -> "<<err);

    try {
        strm.next_in = (z_const Bytef*)in;
        strm.avail_in = inlen;

        while(true) {
            // this number must be smaller than test data size
            const size_t inc = 128;

            out.resize(strm.total_out+inc);

            strm.next_out = (z_const Bytef*)&out[strm.total_out];
            strm.avail_out = inc;

            err = deflate(&strm, strm.avail_in ? Z_NO_FLUSH : Z_FINISH);
            if(err==Z_STREAM_END)
                break;
            else if(err && strm.msg)
                throw std::runtime_error(SB()<<"deflate() : "<<strm.msg);
            else if(err)
                throw std::runtime_error(SB()<<"deflate() -> "<<err);
        }


        // hide unused
        out.resize(out.size()-strm.avail_out);

        strm.avail_out = 0;
    }catch(...){
        deflateEnd(&strm);
        throw;
    }

    err = deflateEnd(&strm);
    if(err!=Z_OK)
        throw std::runtime_error(SB()<<"deflateEnd() -> "<<err);
}

void zinflate(std::vector<char>& out, const char *in, size_t inlen)
{
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    int err = inflateInit(&strm);

    if(err!=Z_OK)
        throw std::runtime_error(SB()<<"inflateInit() -> "<<err);

    try {
        strm.next_in = (z_const Bytef*)in;
        strm.avail_in = inlen;

        while(true) {
            // this number must be smaller than test data size
            const size_t inc = 128;

            out.resize(strm.total_out+inc);

            strm.next_out = (z_const Bytef*)&out[strm.total_out];
            strm.avail_out = inc;

            err = inflate(&strm, strm.avail_in ? Z_NO_FLUSH : Z_FINISH);
            if(err==Z_STREAM_END)
                break;
            else if(err && strm.msg)
                throw std::runtime_error(SB()<<"inflate() : "<<strm.msg);
            else if(err)
                throw std::runtime_error(SB()<<"inflate() -> "<<err);
        }

        // hide unused
        out.resize(out.size()-strm.avail_out);

        strm.avail_out = 0;
    }catch(...){
        inflateEnd(&strm);
        throw;
    }

    err = inflateEnd(&strm);
    if(err!=Z_OK)
        throw std::runtime_error(SB()<<"inflateEnd() -> "<<err);
}
