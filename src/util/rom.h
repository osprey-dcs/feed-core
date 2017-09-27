#ifndef ROM_H
#define ROM_H

#include <list>
#include <string>

#include <epicsTypes.h>

struct ROMDescriptor
{
    enum type_t {
        Invalid=0,
        Text=1,
        BigInt=2, // printed as string
        JSON=3,
    } type;

    std::string value;

    ROMDescriptor() :type(Invalid) {}
    ROMDescriptor(type_t t, const std::string& v)
        :type(t), value(v)
    {}
};

struct ROM {
    typedef std::list<ROMDescriptor> infos_t;
    infos_t infos;

    // process byte array as ROM image.
    // replace our infos
    void parse(const char* buf, size_t buflen);

    // generate a ROM image based on our infos.
    // throws std::runtime_error on failure, including insufficient space
    size_t prepare(epicsUInt32* buf, size_t count);
    size_t prepare(char* buf, size_t buflen);

    typedef infos_t::const_iterator const_iterator;
    const_iterator begin() const { return infos.begin(); }
    const_iterator end() const { return infos.end(); }

    void push_back(ROMDescriptor::type_t t, const std::string& value)
    {
        infos.push_back(ROMDescriptor(t, value));
    }
};

#endif // ROM_H
