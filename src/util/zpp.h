#ifndef ZPP_H
#define ZPP_H

#include <stdlib.h>

#include <vector>

// compress input and store in output.
// existing contents of output are replaced
void zdeflate(std::vector<char>& out, const char *in, size_t inlen, int lvl=6);

// uncompress input and store in output.
// existing contents of output are replaced
void zinflate(std::vector<char>& out, const char *in, size_t inlen);

#endif // ZPP_H
