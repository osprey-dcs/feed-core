#ifndef JBLOB_H
#define JBLOB_H

#include <ostream>
#include <string>
#include <map>

#include <epicsTypes.h>

struct JRegister {
    std::string name,
                description;
    epicsUInt32 base_addr;
    epicsUInt8  addr_width,
                data_width;
    enum sign_t {
        Unsigned, Signed
    } sign;
    bool readable,
         writable;

    void clear() {
        name.clear();
        description.clear();
        base_addr = addr_width = data_width = 0u;
        sign = Unsigned;
        readable = writable = false;
    }

    JRegister() {clear();}
};

struct JBlob {
    typedef std::map<std::string, JRegister> registers_t;
    registers_t registers;

    // Parse and replace current contents
    void parseFile(const char *name);
    void parse(const char *buf);
    void parse(const char *buf, size_t buflen);

    typedef registers_t::const_iterator const_iterator;
    const_iterator begin() const { return registers.begin(); }
    const_iterator end() const { return registers.end(); }
    const_iterator find(const std::string& k) const { return registers.find(k); }

    const JRegister& operator[](const std::string& name) const;
};

std::ostream& operator<<(std::ostream& strm, const JBlob& blob);
std::ostream& operator<<(std::ostream& strm, const JRegister& reg);

#endif // JBLOB_H
