#ifndef ZPP_H
#define ZPP_H

#include <stdlib.h>

#include <vector>

#include <shareLib.h>

// compress input and store in output.
// existing contents of output are replaced
epicsShareExtern void zdeflate(std::vector<char>& out, const char *in, size_t inlen, int lvl=6);

// uncompress input and store in output.
// existing contents of output are replaced
epicsShareExtern void zinflate(std::vector<char>& out, const char *in, size_t inlen);

#endif // ZPP_H
