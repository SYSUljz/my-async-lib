#ifndef ANT_TYPE
#define ANT_TYPE
enum EventType { EVENT_ACCEPT, EVENT_READ, EVENT_WRITE, EVENT_CLOSE, EVENT_TIMER };

struct IOHandler {
  virtual void on_complete(int res, uint32_t flages) = 0;
  virtual ~IOHandler() = default;
};

#endif
// Todo: use Clang to check Google C++ Style Guide