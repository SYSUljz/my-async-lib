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
        co_await with_timeout(ctx, std::chrono::seconds(5), [&]() {
          return handle_http_client(ctx, fd);
        });
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
  char buffer[BUFFER_SIZE];
  size_t read_idx = 0;
  size_t write_idx = 0;
  HttpParseState state = EXPECT_REQUIRE_LINE;
  HttpRequest req;
  while (true) {
    std::string_view unparsed_data(buffer + read_idx, write_idx - read_idx);
    ParseResult result = try_parse_http(unparsed_data, state, req);
    if (result.status == PARSE_NEED_MORE_DATE) {
      read_idx += result.consumed_bytes;

      if (read_idx > 0) {
        if (read_idx < write_idx) {
          std::memmove(buffer, buffer + read_idx, write_idx - read_idx);
          write_idx = write_idx - read_idx;
          read_idx = 0;
        } else {
          read_idx = 0;
          write_idx = 0;
        }
      }

      if (write_idx >= BUFFER_SIZE) {
        co_await CloseAwaiter {ctx, client_fd};
        co_return;
      }

      // Read from socket
      int read_bytes =
          co_await ReadAwaiter {ctx, client_fd, buffer + write_idx, static_cast<int>(BUFFER_SIZE - write_idx)};
      if (read_bytes <= 0) {
        if (read_bytes == -ECANCELED) {
          std::cout << "[Server] Client fd " << client_fd << " timed out (5s), safely closing connection." << std::endl;
        } else {
          std::cout << "[Server] Client fd " << client_fd << " disconnected." << std::endl;
        }
        co_await CloseAwaiter {ctx, client_fd};
        co_return;
      }

      write_idx += read_bytes;
      continue;
    } else if (result.status == PARSE_SUCCESS) {
      read_idx += result.consumed_bytes;

      bool keep_alive = false;
      auto it = req.headers.find("Connection");
      if (it != req.headers.end()) {
        if (it->second == "keep-alive" || it->second == "Keep-Alive") {
          keep_alive = true;
        }
      } else if (req.version == "HTTP/1.1") {
        keep_alive = true;
      }

      std::string_view body = "Hello, C++20 io_uring Web Server!";
      auto format_result = std::format_to_n(buffer, BUFFER_SIZE,
                                            "HTTP/1.1 200 OK\r\n"
                                            "Server: MyAwesomeServer/1.0\r\n"
                                            "Content-Length: {}\r\n"
                                            "Content-Type: text/plain\r\n"
                                            "Connection: {}\r\n"
                                            "\r\n"
                                            "{}",
                                            body.size(), keep_alive ? "keep-alive" : "close", body);
      size_t response_len = format_result.size;
      co_await WriteAwaiter {ctx, client_fd, buffer, static_cast<int>(response_len)};

      if (keep_alive) {
        // Reset state
        state = EXPECT_REQUIRE_LINE;
        req = HttpRequest {};
        if (read_idx < write_idx) {
          std::memmove(buffer, buffer + read_idx, write_idx - read_idx);
          write_idx = write_idx - read_idx;
          read_idx = 0;
        } else {
          read_idx = 0;
          write_idx = 0;
        }
        continue;
      }

      co_await CloseAwaiter {ctx, client_fd};
      co_return;
    } else {
      co_await CloseAwaiter {ctx, client_fd};
      co_return;
    }
  }
}

#endif