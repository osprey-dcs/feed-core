
#include <iostream>
#include <stdexcept>

#include <string.h>

#include <osiSock.h>
#include <epicsTypes.h>

#include <errlog.h>

#include "utils.h"
#include "zpp.h"
#include "rom.h"

static
char hexchar(unsigned I)
{
    I&=0xf;
    if(I<=9)
        return '0'+I;
    else
        return 'a'+I-10;
}

static
unsigned unhexchar(char c)
{
    if(c>='0' && c<='9')
        return c-'0';
    else if(c>='A' && c<='F')
        return c-'A'+10;
    else if(c>='a' && c<='f')
        return c-'a'+10;
    else
        return 0;
}

void ROM::parse(const char* buf, size_t buflen, bool swap)
{
    const char* const orig = buf;

    infos_t temp;

    while(buflen>=4) {
        // only lower 2 bytes are used
        epicsUInt32 val(*reinterpret_cast<const epicsUInt32*>(buf));
        if(swap)
            val = ntohl(val);

        if(val&0xffff0000) {
            throw std::runtime_error("Does not look like ROM contents (high word set)");
        }

        unsigned type = val>>14;
        unsigned size = (val&0x3fff)*4u; // units of bytes (including unused upper bytes)

        buf+=4;
        buflen-=4;

        if(type==0)
            break;

        if(size>buflen) {
            errlogPrintf("Warning: FEED ROM contents truncated at %u (remaining %u)."
                         "  ignoring a type=%u size=%u bytes\n",
                         unsigned(buf-orig), unsigned(buflen), type, size);
            break;
        }

        std::vector<char> contents(size/2u); // copy excluding unused upper bytes

        for(unsigned i=0; i<contents.size(); i+=2) {
            if(swap) {
                contents[i+0] = buf[2*i+2];
                contents[i+1] = buf[2*i+3];
            } else {
                contents[i+0] = buf[2*i+1];
                contents[i+1] = buf[2*i+0];
            }
        }

        ROMDescriptor desc;
        desc.type = (ROMDescriptor::type_t)type;

        switch(desc.type) {
        case ROMDescriptor::Invalid:
            break; // already handled above

        case ROMDescriptor::Text:
            desc.value = std::string(&contents[0], strnlen(&contents[0], contents.size()));
            break;

        case ROMDescriptor::BigInt: {
            std::vector<char> cbuf(contents.size()*2u); // 2 hex chars per byte

            for(unsigned i=0; i<contents.size(); i++) {
                unsigned char B = contents[i];

                cbuf[2*i+0] = hexchar(B>>4);
                cbuf[2*i+1] = hexchar(B>>0);
            }
            desc.value = std::string(&cbuf[0], cbuf.size());
            break;
        }

        case ROMDescriptor::JSON: {
            std::vector<char> json;

            zinflate(json, &contents[0], contents.size());

            desc.value = std::string(&json[0], json.size());
            break;
        }
        }

        temp.push_back(desc);

        buf += size;
        buflen -= size;
    }

    infos.swap(temp);
}

size_t ROM::prepare(epicsUInt32* buf, size_t count)
{
    size_t ret = prepare(reinterpret_cast<char*>(buf), count*4u);
    for(size_t i=0; i<count; i++) {
        buf[i] = ntohl(buf[i]);
    }
    return ret/4u;
}

size_t ROM::prepare(char* buf, size_t buflen)
{
    const char* const orig = buf;
    const size_t origlen = buflen;

    for(infos_t::const_iterator it=infos.begin(), end=infos.end(); it!=end; ++it)
    {
        std::vector<char> contents;

        const ROMDescriptor& info = *it;
        switch(info.type) {
        case ROMDescriptor::Invalid:
            throw std::logic_error("ROM can't contain Invalid ROMDescriptor");

        case ROMDescriptor::Text:
            contents.resize(info.value.size());

            memcpy(&contents[0], info.value.c_str(), contents.size());

            break;
        case ROMDescriptor::BigInt:
            contents.resize(info.value.size()/2u, 0u);
            if(info.value.size()%2u)
                contents.resize(contents.size()+1, 0u);

            for(size_t i=0; i<info.value.size(); i++) {
                unsigned val =unhexchar(info.value[i]);
                if((i%2u)==0)
                    val <<= 4;
                contents[i/2] |= val;
            }
            break;
        case ROMDescriptor::JSON:
            zdeflate(contents, info.value.c_str(), info.value.size(), 9);
            break;
        }

        if(contents.size()%2)
            contents.push_back('\0');

        // check that word size in header won't overflow
        if(contents.size()/2u >= 0x4000)
            throw std::runtime_error(SB()<<"Descriptor type="<<info.type<<" too large size="<<contents.size());

        // check for space in output buffer
        if(4u+contents.size()*2u>buflen)
            throw std::runtime_error(SB()<<"Not enough space to encode ROM contents at "
                                     <<(buf-orig)<<" have "<<buflen<<" need "<<(4u+contents.size()*2u));

        epicsUInt32 header = (info.type<<14) | contents.size()/2u;

        *reinterpret_cast<epicsUInt32*>(buf) = htonl(header);
        buf += 4;
        buflen -= 4;

        for(size_t i=0; i<contents.size(); i+=2)
        {
            buf[2*i+0] = buf[2*i+1] = 0;
            buf[2*i+2] = contents[i+0];
            buf[2*i+3] = contents[i+1];
        }

        buf += contents.size()*2u;
        buflen -= contents.size()*2u;

        assert(buf-orig+buflen == origlen);
    }

    if(buflen<4) {
        throw std::runtime_error("Not enough space to add End Descriptor");
        *reinterpret_cast<epicsUInt32*>(buf) = 0;
        buf+=4;
        buflen-=4;
    }

    return buf-orig;
}
