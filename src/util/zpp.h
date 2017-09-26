#ifndef ZPP_H
#define ZPP_H

#include <stdlib.h>

#include <vector>

void zdeflate(std::vector<char>& out, const char *in, size_t inlen, int lvl=6);

void zinflate(std::vector<char>& out, const char *in, size_t inlen);

#endif // ZPP_H
