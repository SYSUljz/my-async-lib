#include "context.hpp"
#include "context/service.hpp"
#include "type.hpp"
struct Acceptor : public IOHandler {
  Acceptor(Context& ctx, int server_socket, std::function<void(int)> on_accept)
      : ctx_(ctx),
        server_socket_(server_socket),
        on_accept_cb_(std::move(on_accept)),
        service_(ctx_.UseService<IOuringAcceptService>()) {}

  void start() { service_.submit_multishot_accept(server_socket_, static_cast<IOHandler*>(this)); }
  void on_complete(int res, uint32_t flags) override {
    if (res > 0) {
      on_accept_cb_(res);
    } else {
      throw std::runtime_error("Accept error");
    }

    if (!(flags & IORING_CQE_F_MORE)) {
      service_.submit_multishot_accept(server_socket_, static_cast<IOHandler*>(this));
    }
  }

 private:
  Context& ctx_;
  int server_socket_;
  std::function<void(int)> on_accept_cb_;
  IOuringAcceptService& service_;
};