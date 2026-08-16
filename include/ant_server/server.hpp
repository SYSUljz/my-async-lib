#ifndef ANT_SERVER
#define ANT_SERVER

#include <liburing.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <coroutine>
#include <cstring>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#include <netinet/in.h>

#include "ant_server/awaiter/socket_awaiter.hpp"
#include "ant_server/awaiter/timeput_awaiter.hpp"
#include "ant_server/handler/acceptor.hpp"
#include "ant_server/http/parser.hpp"
#include "ant_server/scheduler/executor.hpp"
#include "ant_server/scheduler/timer_keeper.hpp"
#include "ant_server/type.hpp"

static constexpr int kBufferSize = 16000;

HttpTask handle_http_client(Context& ctx, int client_fd);

class Server {
 public:
  static constexpr int kUringSize = 256;

  Server(Context& ctx, int domain, int port, int service, int protocol, int backlog, u_long interface)
      : ctx_(ctx),
        domain_(domain),
        port_(port),
        service_(service),
        protocol_(protocol),
        backlog_(backlog),
        interface_(interface) {
    address_.sin_family = domain_;
    address_.sin_port = htons(port_);
    address_.sin_addr.s_addr = htonl(interface_);

    server_socket_ = socket(domain_, service_, protocol_);
    if (server_socket_ < 0) {
      perror("Failed to initialize/connect to socket...");
      exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&address_), sizeof(address_)) < 0) {
      perror("Failed to bind socket...");
      exit(EXIT_FAILURE);
    }

    if (listen(server_socket_, backlog_) < 0) {
      perror("Failed to start listening...");
      exit(EXIT_FAILURE);
    }

    if (!ctx_.GetScheduler() && !g_scheduler) {
      default_executor_ = std::make_unique<WorkStealingExecutor>(1);
      default_executor_->Start();
      default_timer_keeper_ = std::make_unique<TimerKeeper>(*default_executor_);
      default_timer_keeper_->Start();
    }

    acceptor_ = std::make_unique<Acceptor>(ctx_, server_socket_, [this](int client_fd) {
      TimerKeeper& tk = default_timer_keeper_ ? *default_timer_keeper_
                                              : (ctx_.GetScheduler() ? ctx_.GetScheduler()->GetTimerKeeper()
                                                                     : ant_server::GetEffectiveTimerKeeper());

      [](Context& ctx, TimerKeeper& timer_keeper, int fd) -> DetachedTask {
        co_await with_timeout(timer_keeper, std::chrono::seconds(5), [&]() { return handle_http_client(ctx, fd); });
      }(ctx_, tk, client_fd);
    });
    acceptor_->Start();
  }

  ~Server() {
    if (server_socket_ >= 0) {
      close(server_socket_);
    }
    if (default_timer_keeper_) {
      default_timer_keeper_->Stop();
    }
    if (default_executor_) {
      default_executor_->Stop();
    }
  }

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

 private:
  int domain_;
  int port_;
  int service_;
  int protocol_;
  int backlog_;
  u_long interface_;
  int server_socket_;
  struct sockaddr_in address_;
  Context& ctx_;
  std::unique_ptr<Acceptor> acceptor_;
  std::unique_ptr<WorkStealingExecutor> default_executor_;
  std::unique_ptr<TimerKeeper> default_timer_keeper_;
};

inline HttpTask handle_http_client(Context& ctx, int client_fd) {
  butil::IOBuf read_buf;
  HttpParser parser;
  HttpRequest req;

  while (true) {
    ParseResult result = parser.parse(read_buf, req);

    if (result.status == PARSE_NEED_MORE_DATA) {
      int read_bytes = co_await ReadAwaiter {ctx, client_fd, read_buf};
      if (read_bytes <= 0) {
        if (read_bytes == -ECANCELED) {
          std::cout << "[Server] Client fd " << client_fd << " timed out (5s), safely closing connection." << std::endl;
        } else {
          std::cout << "[Server] Client fd " << client_fd << " disconnected." << std::endl;
        }
        co_await CloseAwaiter {ctx, client_fd};
        co_return;
      }
      continue;
    } else if (result.status == PARSE_SUCCESS) {
      read_buf.pop_front(result.consumed_bytes);

      bool keep_alive = req.keep_alive;
      std::string body = "Hello, C++20 io_uring Web Server with IOBuf & llhttp!";

      butil::IOBuf response_buf;
      std::string header_str = std::format(
          "HTTP/1.1 200 OK\r\n"
          "Server: MyAwesomeServer/1.0\r\n"
          "Content-Length: {}\r\n"
          "Content-Type: text/plain\r\n"
          "Connection: {}\r\n"
          "\r\n"
          "{}",
          body.size(), keep_alive ? "keep-alive" : "close", body);
      response_buf.append(header_str);

      co_await IOBufWriteAwaiter {ctx, client_fd, response_buf};

      if (keep_alive) {
        parser.reset();
        req = HttpRequest {};
        continue;
      } else {
        co_await CloseAwaiter {ctx, client_fd};
        co_return;
      }
    } else {
      co_await CloseAwaiter {ctx, client_fd};
      co_return;
    }
  }
}

#endif
