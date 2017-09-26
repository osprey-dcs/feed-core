#ifndef UTILS_H
#define UTILS_H

#include <sstream>

// in-line string builder (eg. for exception messages)
struct SB {
    std::ostringstream strm;
    SB() {}
    operator std::string() const { return strm.str(); }
    template<typename T>
    SB& operator<<(T i) { strm<<i; return *this; }
};

#endif // UTILS_H
