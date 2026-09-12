#pragma once
#include "stochlab/json.hpp"
namespace stochlab {
using json = nlohmann::json;
json request(const json &input);
} // namespace stochlab
