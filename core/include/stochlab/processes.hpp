#pragma once

#include "stochlab/json.hpp"

namespace stochlab {
using json = nlohmann::json;

// Validated, reproducibly seeded simulation with bounded representative series.
// Invalid parameters and resource limits throw std::invalid_argument.
json simulate(const json &request);
void test_processes();
void validate_processes();
} // namespace stochlab
