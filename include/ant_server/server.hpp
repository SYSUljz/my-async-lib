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
#include "ant_server/type.hpp"

static constexpr int BUFFER_SIZE = 16000;

HttpTask handle_http_client(Context& ctx, int client_fd);

class Server {
 public:
  static constexpr int URING_SIZE = 256;

  Server(Context& ctx, int domain, int port, int service, int protocol, int backlog, u_long interface)
      : ctx_(ctx),
        domain(domain),
        port(port),
        service(service),
        protocol(protocol),
        backlog(backlog),
        interface(interface) {
    address.sin_family = domain;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(interface);

    server_socket = socket(domain, service, protocol);
    if (server_socket < 0) {
      perror("Failed to initialize/connect to socket...");
      exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_socket, (struct sockaddr*)&address, sizeof(address)) < 0) {
      perror("Failed to bind socket...");
      exit(EXIT_FAILURE);
    }

    if (listen(server_socket, backlog) < 0) {
      perror("Failed to start listening...");
      exit(EXIT_FAILURE);
    }

    acceptor_ = std::make_unique<Acceptor>(ctx_, server_socket, [this](int client_fd) {
      [](Context& ctx, int fd) -> DetachedTask {
        co_await with_timeout(ctx, std::chrono::seconds(5), [&]() { return handle_http_client(ctx, fd); });
      }(ctx_, client_fd);
    });
    acceptor_->Start();
  }
  ~Server() {
    if (server_socket >= 0) {
      close(server_socket);
    }
  }
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

 private:
  int domain;
  int port;
  int service;
  int protocol;
  int backlog;
  u_long interface;
  int server_socket;
  struct sockaddr_in address;
  Context& ctx_;
  std::unique_ptr<Acceptor> acceptor_;
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