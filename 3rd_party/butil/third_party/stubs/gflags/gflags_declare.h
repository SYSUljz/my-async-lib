#ifndef GFLAGS_GFLAGS_DECLARE_H_
#define GFLAGS_GFLAGS_DECLARE_H_

#include <cstdint>
#include <string>

namespace gflags {
template <typename T, typename Fn>
inline bool RegisterFlagValidator(const T*, Fn) {
    return true;
}
}  // namespace gflags

namespace google {
inline bool SymbolizeAddress(void*, void*) { return false; }
}  // namespace google

#ifndef GFLAGS_NAMESPACE
#define GFLAGS_NAMESPACE gflags
#endif

#define DECLARE_bool(name) extern bool FLAGS_##name
#define DECLARE_int32(name) extern int32_t FLAGS_##name
#define DECLARE_int64(name) extern int64_t FLAGS_##name
#define DECLARE_uint64(name) extern uint64_t FLAGS_##name
#define DECLARE_double(name) extern double FLAGS_##name
#define DECLARE_string(name) extern std::string FLAGS_##name

#define DEFINE_bool(name, val, txt) bool FLAGS_##name = val
#define DEFINE_int32(name, val, txt) int32_t FLAGS_##name = val
#define DEFINE_int64(name, val, txt) int64_t FLAGS_##name = val
#define DEFINE_uint64(name, val, txt) uint64_t FLAGS_##name = val
#define DEFINE_double(name, val, txt) double FLAGS_##name = val
#define DEFINE_string(name, val, txt) std::string FLAGS_##name = val

#endif  // GFLAGS_GFLAGS_DECLARE_H_
