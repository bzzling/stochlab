#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace stochlab {

// Explicit arithmetic makes the stream independent of the standard library RNG.
// This is a scientific simulation generator, not a cryptographic generator.
class Rng {
public:
  explicit Rng(std::uint64_t seed) : state_(seed) {}

  std::uint64_t next_u64() {
    std::uint64_t z = (state_ += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
  }

  double uniform() {
    // 52 bits and a half-unit offset keep both endpoints strictly excluded.
    return (static_cast<double>(next_u64() >> 12) + 0.5) * 0x1.0p-52;
  }

  std::uint64_t integer(std::uint64_t n) {
    if (n == 0)
      throw std::invalid_argument("integer draw requires n > 0");
    const std::uint64_t threshold = -n % n;
    for (;;) {
      const std::uint64_t r = next_u64();
      if (r >= threshold)
        return r % n;
    }
  }

  double exponential(double rate) {
    if (!(rate > 0) || !std::isfinite(rate))
      throw std::invalid_argument("exponential rate must be finite and positive");
    return -std::log(uniform()) / rate;
  }

  double normal() {
    if (has_spare_) {
      has_spare_ = false;
      return spare_;
    }
    constexpr double tau = 6.283185307179586476925286766559;
    const double radius = std::sqrt(-2.0 * std::log(uniform()));
    const double angle = tau * uniform();
    spare_ = radius * std::sin(angle);
    has_spare_ = true;
    return radius * std::cos(angle);
  }

private:
  std::uint64_t state_;
  double spare_ = 0;
  bool has_spare_ = false;
};

} // namespace stochlab
