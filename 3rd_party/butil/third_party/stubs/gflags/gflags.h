#ifndef GFLAGS_GFLAGS_H_
#define GFLAGS_GFLAGS_H_

#include <cstdint>
#include <string>
#include "gflags/gflags_declare.h"

#define DEFINE_bool(name, val, txt) bool FLAGS_##name = val
#define DEFINE_int32(name, val, txt) int32_t FLAGS_##name = val
#define DEFINE_int64(name, val, txt) int64_t FLAGS_##name = val
#define DEFINE_uint64(name, val, txt) uint64_t FLAGS_##name = val
#define DEFINE_double(name, val, txt) double FLAGS_##name = val
#define DEFINE_string(name, val, txt) std::string FLAGS_##name = val

#endif  // GFLAGS_GFLAGS_H_
