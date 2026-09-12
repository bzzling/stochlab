#include "stochlab/api.hpp"
#include <emscripten/emscripten.h>
#include <string>
extern "C" {
EMSCRIPTEN_KEEPALIVE const char *sl_request(const char *input) {
  static std::string output;
  try {
    output = stochlab::request(stochlab::json::parse(input)).dump();
  } catch (const std::exception &e) {
    output = stochlab::json{{"error", e.what()}}.dump();
  }
  return output.c_str();
}
}
