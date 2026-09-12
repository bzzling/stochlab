#include "stochlab/api.hpp"
#include "stochlab/exchange.hpp"
#include "stochlab/processes.hpp"
#include <memory>
#include <stdexcept>
namespace stochlab {
json request(const json &input) {
  static std::unique_ptr<Exchange> exchange;
  const auto action = input.value("action", std::string("simulate"));
  if (action == "simulate")
    return simulate(input);
  if (action == "exchange-init") {
    exchange = std::make_unique<Exchange>(input);
    return exchange->snapshot();
  }
  if (action == "exchange-step" || action == "exchange-snapshot") {
    if (!exchange)
      throw std::invalid_argument("Initialize the exchange first.");
    if (action == "exchange-snapshot")
      return exchange->snapshot();
    const auto n = input.value("events", 10.0);
    if (!std::isfinite(n) || n != std::floor(n) || n < 1 || n > 1000000)
      throw std::invalid_argument("events must be an integer in [1, 1000000].");
    int remaining = static_cast<int>(n);
    while (remaining > 250000) {
      exchange->advance(250000);
      remaining -= 250000;
    }
    return exchange->advance(remaining);
  }
  throw std::invalid_argument("Unknown engine action: " + action);
}
} // namespace stochlab
