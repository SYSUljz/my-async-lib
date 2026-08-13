#pragma once
#include <string>
#include <string_view>

#include <absl/container/flat_hash_map.h>
enum HttpParseState { EXPECT_REQUIRE_LINE, EXPECT_HEADERS, EXPECT_BODY, PARSE_DONE };

enum ParseStatus { PARSE_SUCCESS, PARSE_NEED_MORE_DATE, PARSE_ERROR };

struct ParseResult {
  ParseStatus status;
  size_t consumed_bytes;
};

struct HttpRequest {
  std::string_view method;
  std::string_view uri;
  std::string_view version;
  absl::flat_hash_map<std::string_view, std::string_view> headers;
  size_t content_length = 0;
  std::string_view body;
};

ParseResult try_parse_http(std::string_view unparsed_data, HttpParseState& state, HttpRequest& req) {
  size_t total_consumed = 0;
  std::string_view view = unparsed_data;
  while (state != PARSE_DONE) {
    if (state == EXPECT_REQUIRE_LINE) {
      size_t crlf_pos = view.find("\r\n");
      if (crlf_pos == std::string_view::npos) {
        return {PARSE_NEED_MORE_DATE, total_consumed};
      }
      std::string_view line = view.substr(0, crlf_pos);
      size_t space1 = line.find(' ');
      size_t space2 = line.find(' ', space1 + 1);
      if (space1 == std::string_view::npos || space2 == std::string_view::npos) {
        return {PARSE_ERROR, 0};
      }

      req.method = line.substr(0, space1);
      req.uri = line.substr(space1 + 1, space2 - space1 - 1);
      req.version = line.substr(space2 + 1);
      size_t consumed = crlf_pos + 2;
      view.remove_prefix(consumed);
      total_consumed += consumed;
      state = EXPECT_HEADERS;

    } else if (state == EXPECT_HEADERS) {
      size_t crlf_pos = view.find("\r\n");
      if (crlf_pos == std::string_view::npos) {
        return {PARSE_NEED_MORE_DATE, total_consumed};
      }
      if (crlf_pos == 0) {
        size_t consumed = 2;
        view.remove_prefix(consumed);
        total_consumed += consumed;

        auto it = req.headers.find("Content-Length");
        if (it != req.headers.end()) {
          req.content_length = std::stoull(std::string(it->second));
          state = EXPECT_BODY;
        } else {
          state = PARSE_DONE;
        }
        continue;
      }

      std::string_view line = view.substr(0, crlf_pos);
      size_t colon_pos = line.find(':');
      if (colon_pos != std::string_view::npos) {
        std::string_view key = line.substr(0, colon_pos);
        std::string_view value = line.substr(colon_pos + 1, crlf_pos - colon_pos - 1);
        while (!value.empty() && value.front() == ' ') {
          value.remove_prefix(1);
        }
        req.headers[key] = value;
      }
      size_t consumed = crlf_pos + 2;
      view.remove_prefix(consumed);
      total_consumed += consumed;
    } else if (state == EXPECT_BODY) {
      if (view.length() < req.content_length) {
        return {PARSE_NEED_MORE_DATE, total_consumed};
      }
      req.body = view.substr(0, req.content_length);

      size_t consumed = req.content_length;
      view.remove_prefix(consumed);
      total_consumed += consumed;
      state = PARSE_DONE;
    }
  }

  return {PARSE_SUCCESS, total_consumed};
}