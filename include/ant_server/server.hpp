#ifndef ANT_SERVER
#define ANT_SERVER

#include <liburing.h>
#include <unistd.h>

#include <coroutine>
#include <format>
#include <functional>
#include <iostream>
#include <string>

#include <netinet/in.h>

#include "awaiter.hpp"
#include "http/parser.hpp"
#include "type.hpp"
static constexpr int BUFFER_SIZE = 16000;

DetachedTask handle_http_client(io_uring& ring, int client_fd) {
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
      int read_bytes = co_await ReadAwaiter {ring, client_fd, buffer + write_idx, static_cast<int>(BUFFER_SIZE - write_idx)};
      if (read_bytes <= 0) {
        co_await CloseAwaiter {ring, client_fd};
        co_return;
      }
      write_idx += read_bytes;
      continue;
    } else if (result.status == PARSE_SUCCESS) {
      read_idx += result.consumed_bytes;
      std::string_view body = "Hello, C++20 io_uring Web Server!";
      auto format_result = std::format_to_n(buffer, BUFFER_SIZE,
                                            "HTTP/1.1 200 OK\r\n"
                                            "Server: MyAwesomeServer/1.0\r\n"
                                            "Content-Length: {}\r\n"
                                            "Content-Type: text/plain\r\n"
                                            "\r\n"
                                            "{}",
                                            body.size(), body);
      size_t response_len = format_result.size;
      co_await WriteAwaiter {ring, client_fd, buffer, static_cast<int>(response_len)};
      co_await CloseAwaiter {ring, client_fd};
      co_return;
    } else {
      co_await CloseAwaiter {ring, client_fd};
      co_return;
    }
  }
}

class Server {
 public:
  static constexpr int URING_SIZE = 256;

  Server(int domain, int port, int service, int protocol, int backlog, u_long interface)
      : domain(domain), port(port), service(service), protocol(protocol), backlog(backlog), interface(interface) {
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
  }
  ~Server() {
    if (server_socket >= 0) {
      close(server_socket);
    }
  }
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  void launch() {
    struct io_uring ring;

    if (io_uring_queue_init(URING_SIZE, &ring, 0) < 0) {
      perror("Failed to initialize io_uring");
      exit(EXIT_FAILURE);
    }

    if (io_uring_register_files_sparse(&ring, URING_SIZE) < 0) {
      perror("Failed to register sparse files table");
      exit(EXIT_FAILURE);
    }

    submit_multishot_accept(ring);
    std::cout << "listen to port 8012" << std::endl;
    while (1) {
      unsigned head = 0;
      unsigned count = 0;
      struct io_uring_cqe* cqe;
      io_uring_for_each_cqe(&ring, head, cqe) {
        void* user_data = io_uring_cqe_get_data(cqe);

        if (user_data == (void*)EVENT_ACCEPT) {
          int client_index = cqe->res;
          std::cout << "start a new connection" << std::endl;
          if (client_index >= 0) {
            handle_http_client(ring, client_index);
          }

          if (!(cqe->flags & IORING_CQE_F_MORE)) {
            submit_multishot_accept(ring);
          }
        } else if (user_data == (void*)EVENT_CLOSE) {
          std::cout << "close a old connection" << std::endl;
        } else {
          auto* awaiter = static_cast<BaseAwaiter*>(user_data);
          awaiter->res = cqe->res;
          awaiter->handle.resume();
        }

        count++;
      }
      io_uring_cq_advance(&ring, count);
    }
  };

 private:
  int domain;
  int port;
  int service;
  int protocol;
  int backlog;
  u_long interface;
  int server_socket;
  struct sockaddr_in address;

  inline void submit_multishot_accept(struct io_uring& ring) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_multishot_accept_direct(sqe, server_socket, nullptr, nullptr, 0);
    sqe->file_index = IORING_FILE_INDEX_ALLOC;
    io_uring_sqe_set_data(sqe, (void*)EVENT_ACCEPT);
    io_uring_submit(&ring);
  }
};
#endif