#ifndef GOOGLE_PROTOBUF_IO_ZERO_COPY_STREAM_H_
#define GOOGLE_PROTOBUF_IO_ZERO_COPY_STREAM_H_

#include <cstddef>
#include <cstdint>

namespace google {
namespace protobuf {
namespace io {

class ZeroCopyInputStream {
public:
    virtual ~ZeroCopyInputStream() = default;
    virtual bool Next(const void** data, int* size) = 0;
    virtual void BackUp(int count) = 0;
    virtual bool Skip(int count) = 0;
    virtual int64_t ByteCount() const = 0;
};

class ZeroCopyOutputStream {
public:
    virtual ~ZeroCopyOutputStream() = default;
    virtual bool Next(void** data, int* size) = 0;
    virtual void BackUp(int count) = 0;
    virtual int64_t ByteCount() const = 0;
    virtual bool WriteAliasedRaw(const void* data, int size) { return false; }
    virtual bool AllowsAliasing() const { return false; }
};

}  // namespace io
}  // namespace protobuf
}  // namespace google

#endif  // GOOGLE_PROTOBUF_IO_ZERO_COPY_STREAM_H_
