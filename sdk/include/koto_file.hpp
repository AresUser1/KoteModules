#ifndef KOTO_FILE_HPP
#define KOTO_FILE_HPP

#include <string>
#include <vector>
#include <cstdio>
#include "module_abi.h"

namespace koto_file {

inline bool file_exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

inline long get_file_size(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz;
}

} // namespace koto_file

#endif // KOTO_FILE_HPP
