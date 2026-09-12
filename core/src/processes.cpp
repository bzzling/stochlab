#include "stochlab/processes.hpp"

#include "stochlab/rng.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace stochlab {
namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr std::uint64_t max_events = 5'000'000;
constexpr std::size_t max_visible_events = 12'000;

double real(const json &j, const char *key, double fallback) {
  if (!j.contains(key))
    return fallback;
  if (!j.at(key).is_number())
    throw std::invalid_argument(std::string(key) + " must be a finite number");
  const double value = j.at(key).get<double>();
  if (!std::isfinite(value))
    throw std::invalid_argument(std::string(key) + " must be finite");
  return value;
}

double bounded(const json &j, const char *key, double fallback, double low, double high,
               bool strict_low = false) {
  const double value = real(j, key, fallback);
  if (value < low || value > high || (strict_low && value == low))
    throw std::invalid_argument(std::string(key) + " is outside the supported range");
  return value;
}

std::uint64_t integer(const json &j, const char *key, std::uint64_t fallback, std::uint64_t low,
                      std::uint64_t high) {
  const double v = bounded(j, key, static_cast<double>(fallback), static_cast<double>(low),
                           static_cast<double>(high));
  if (std::floor(v) != v)
    throw std::invalid_argument(std::string(key) + " must be an integer");
  return static_cast<std::uint64_t>(v);
}

struct Moments {
  std::uint64_t n = 0;
  double mean = 0;
  double m2 = 0;
  void add(double x) {
    ++n;
    const double d = x - mean;
    mean += d / static_cast<double>(n);
    m2 += d * (x - mean);
  }
  double variance() const { return n > 1 ? m2 / static_cast<double>(n - 1) : 0; }
  double se() const { return n > 1 ? std::sqrt(variance() / static_cast<double>(n)) : 0; }
};

json metric(const char *key, const char *label, double value) {
  return {{"key", key}, {"label", label}, {"value", value}};
}

json compared(const char *key, const char *label, double value, double theory, double se = -1,
              const std::string &note = "") {
  json m = metric(key, label, value);
  m["theory"] = theory;
  if (se >= 0)
    m["se"] = se;
  if (!note.empty())
    m["note"] = note;
  return m;
}

json histogram(const std::vector<double> &values, bool discrete = false) {
  json out = json::array();
  if (values.empty())
    return out;
  const auto [lo_it, hi_it] = std::minmax_element(values.begin(), values.end());
  double low = *lo_it;
  double high = *hi_it;
  std::size_t bins = 40;
  double width = 0;
  if (discrete && high - low < 80) {
    bins = static_cast<std::size_t>(high - low) + 1;
    low -= 0.5;
    width = 1;
  } else if (high == low) {
    bins = 1;
    low -= 0.5;
    width = 1;
  } else {
    width = (high - low) / static_cast<double>(bins);
  }
  std::vector<std::uint64_t> counts(bins, 0);
  for (double x : values) {
    const std::size_t bin = std::min(bins - 1, static_cast<std::size_t>((x - low) / width));
    ++counts[bin];
  }
  for (std::size_t i = 0; i < bins; ++i)
    out.push_back({{"x", low + (static_cast<double>(i) + 0.5) * width},
                   {"count", counts[i]},
                   {"density", static_cast<double>(counts[i]) /
                                   (static_cast<double>(values.size()) * width)}});
  return out;
}

// Marsaglia–Tsang gamma sampler; only positive half-integer shapes are used.
double gamma(Rng &rng, double shape) {
  if (shape < 1)
    return gamma(rng, shape + 1) * std::pow(rng.uniform(), 1.0 / shape);
  const double d = shape - 1.0 / 3;
  const double c = 1 / std::sqrt(9 * d);
  for (;;) {
    const double z = rng.normal();
    const double root = 1 + c * z;
    if (root <= 0)
      continue;
    const double v = root * root * root;
    const double u = rng.uniform();
    if (u < 1 - 0.0331 * z * z * z * z || std::log(u) < 0.5 * z * z + d * (1 - v + std::log(v)))
      return d * v;
  }
}

void moment_metrics(json &out, const Moments &stats, double mean, double variance) {
  out["metrics"].push_back(compared("mean", "Terminal mean", stats.mean, mean, stats.se()));
  out["metrics"].push_back(compared("variance", "Terminal variance", stats.variance(), variance));
}

struct Request {
  std::string model;
  std::uint32_t seed;
  std::size_t paths;
  std::size_t steps;
  double horizon;
  explicit Request(const json &j) {
    if (!j.is_object())
      throw std::invalid_argument("request must be an object");
    if (j.contains("model") && !j["model"].is_string())
      throw std::invalid_argument("model must be a string");
    model = j.value("model", std::string("brownian"));
    seed = static_cast<std::uint32_t>(integer(j, "seed", 42, 0, UINT32_MAX));
    paths = static_cast<std::size_t>(integer(j, "paths", 2000, 1, 1'000'000));
    steps = static_cast<std::size_t>(integer(j, "steps", 256, 1, 8192));
    const double default_t = model == "stationarity" ? 20
                             : (model == "poisson" || model == "compound-poisson" ||
                                model == "ctmc" || model == "hawkes")
                                 ? 10
                                 : 1;
    horizon = bounded(j, "T", default_t, 1e-6, 10000);
  }
  json response() const {
    return {{"model", model},
            {"seed", seed},
            {"paths", paths},
            {"steps", steps},
            {"T", horizon},
            {"algorithm", ""},
            {"series", json::array()},
            {"histogram", json::array()},
            {"metrics", json::array()},
            {"warnings", json::array()}};
  }
};

void brownian(const json &j, const Request &r, json &out) {
  const double barrier = bounded(j, "barrier", 1, 0, 1e6);
  const bool is_qv = r.model == "qv";
  const std::size_t visible = std::min<std::size_t>(24, r.paths);
  const double dt = r.horizon / static_cast<double>(r.steps);
  const double scale = std::sqrt(dt);
  Rng rng(r.seed);
  Moments terminals, maxima, qvs;
  std::vector<double> samples;
  samples.reserve(r.paths);
  std::size_t hits = 0;
  for (std::size_t p = 0; p < r.paths; ++p) {
    double x = 0, maximum = 0, qv = 0;
    if (p < visible) {
      std::vector<double> t(r.steps + 1), y(r.steps + 1);
      json hit = barrier == 0 ? json(0) : json(nullptr);
      for (std::size_t k = 1; k <= r.steps; ++k) {
        const double old = x;
        x += scale * rng.normal();
        qv += (x - old) * (x - old);
        // Conditional Brownian bridge maximum over this grid interval.
        const double bridge_max =
            0.5 * (old + x + std::sqrt((x - old) * (x - old) - 2 * dt * std::log(rng.uniform())));
        maximum = std::max(maximum, bridge_max);
        t[k] = static_cast<double>(k) * dt;
        y[k] = x;
        if (hit.is_null() && x >= barrier)
          hit = k;
      }
      t.back() = r.horizon;
      out["series"].push_back({{"t", t}, {"y", y}, {"hit", hit}});
      if (p == 0) {
        // Divisors give genuinely nested, uniform partitions of this one path.
        std::vector<std::size_t> partitions;
        for (std::size_t n = 1; n <= r.steps; n *= 2) {
          if (r.steps % n == 0)
            partitions.push_back(n);
          if (n > r.steps / 2)
            break;
        }
        if (partitions.back() != r.steps)
          partitions.push_back(r.steps);
        // If steps is not a power of two, the last partition still refines all
        // earlier divisors because all earlier divisors are powers of two.
        out["qv"] = json::array();
        for (const auto n : partitions) {
          double v = 0;
          const auto stride = r.steps / n;
          for (std::size_t k = stride; k <= r.steps; k += stride) {
            const double delta = y[k] - y[k - stride];
            v += delta * delta;
          }
          out["qv"].push_back({{"partitions", n}, {"value", v}, {"theory", r.horizon}});
        }
      }
    } else {
      x = std::sqrt(r.horizon) * rng.normal();
      if (is_qv) {
        // Orthogonal Gaussian projection: terminal and residual sum of squares.
        qv = x * x / static_cast<double>(r.steps);
        if (r.steps > 1)
          qv += 2 * dt * gamma(rng, static_cast<double>(r.steps - 1) / 2);
      } else {
        maximum = 0.5 * (x + std::sqrt(x * x - 2 * r.horizon * std::log(rng.uniform())));
      }
    }
    terminals.add(x);
    samples.push_back(x);
    if (is_qv)
      qvs.add(qv);
    else {
      maxima.add(maximum);
      if (maximum >= barrier)
        ++hits;
    }
  }
  out["algorithm"] = is_qv ? "Exact Gaussian increments on rendered paths; exact Gaussian "
                             "projection / chi-square aggregate QV"
                           : "Exact joint Brownian terminal / maximum sampling; conditional bridge "
                             "maxima on rendered grid";
  out["histogram"] = histogram(samples);
  moment_metrics(out, terminals, 0, r.horizon);
  if (is_qv) {
    out["metrics"].push_back(compared("qv_mean", "Mean quadratic variation", qvs.mean, r.horizon,
                                      r.horizon * std::sqrt(2.0 / (static_cast<double>(r.steps) *
                                                                   static_cast<double>(r.paths)))));
    out["metrics"].push_back(compared("qv_variance", "QV variance", qvs.variance(),
                                      2 * r.horizon * r.horizon / static_cast<double>(r.steps)));
    out["warnings"].push_back(
        "Nested QV values use one rendered path; convergence need not be monotone.");
  } else {
    const double theory = std::erfc(barrier / std::sqrt(2 * r.horizon));
    const double empirical = static_cast<double>(hits) / static_cast<double>(r.paths);
    out["metrics"].push_back(
        compared("hit_probability", "P(maximum ≥ barrier)", empirical, theory,
                 std::sqrt(theory * (1 - theory) / static_cast<double>(r.paths)),
                 "Continuous-time crossing probability; exact bridge sampling includes "
                 "between-grid crossings."));
    out["metrics"].push_back(compared("max_mean", "Mean running maximum", maxima.mean,
                                      std::sqrt(2 * r.horizon / pi), maxima.se()));
    out["warnings"].push_back(
        "Rendered hit indices locate the first grid observation above the barrier. Polyline "
        "reflection interpolates within that segment; continuous hits can occur earlier.");
  }
}

void random_walk(const json &j, const Request &r, json &out) {
  if (static_cast<std::uint64_t>(r.paths) * static_cast<std::uint64_t>(r.steps) > 60'000'000)
    throw std::invalid_argument("random-walk requires paths × steps ≤ 60,000,000");
  const double barrier = bounded(j, "barrier", 1, 0, 1e6);
  const double scale = std::sqrt(r.horizon / static_cast<double>(r.steps));
  Rng rng(r.seed);
  Moments stats;
  std::size_t hits = 0;
  std::vector<double> samples;
  samples.reserve(r.paths);
  for (std::size_t p = 0; p < r.paths; ++p) {
    int position = 0;
    bool crossed = barrier == 0;
    const bool save = p < 24;
    std::vector<double> t, y;
    json hit = crossed ? json(0) : json(nullptr);
    if (save) {
      t.push_back(0);
      y.push_back(0);
    }
    for (std::size_t k = 1; k <= r.steps; ++k) {
      position += (rng.next_u64() & 1U) ? 1 : -1;
      const double x = static_cast<double>(position) * scale;
      if (x >= barrier && !crossed) {
        crossed = true;
        hit = k;
      }
      if (save) {
        t.push_back(r.horizon * static_cast<double>(k) / static_cast<double>(r.steps));
        y.push_back(x);
      }
    }
    const double terminal = static_cast<double>(position) * scale;
    stats.add(terminal);
    samples.push_back(terminal);
    hits += crossed;
    if (save)
      out["series"].push_back({{"t", t}, {"y", y}, {"hit", hit}});
  }
  out["algorithm"] = "Symmetric Bernoulli increments with diffusive scaling √(T / steps)";
  out["histogram"] = histogram(samples);
  moment_metrics(out, stats, 0, r.horizon);
  out["metrics"].push_back(metric("hit_probability", "Grid crossing probability",
                                  static_cast<double>(hits) / static_cast<double>(r.paths)));
  out["metrics"].push_back(metric("step_size", "Scaled step size", scale));
  out["warnings"].push_back("The scaled walk has lattice-valued endpoints; its Gaussian limit is "
                            "approached as steps increase.");
}

void ou(const json &j, const Request &r, json &out) {
  const double theta = bounded(j, "theta", 2, 1e-6, 10000);
  const double mu = bounded(j, "mu", 0, -1e6, 1e6);
  const double sigma = bounded(j, "sigma", 1, 0, 10000);
  const double x0 = bounded(j, "x0", r.model == "stationarity" ? 3 : 0, -1e6, 1e6);
  const double dt = r.horizon / static_cast<double>(r.steps);
  const double decay = std::exp(-theta * dt);
  const double gain = -std::expm1(-theta * dt);
  const double stationary_var = sigma * sigma / (2 * theta);
  const double transition_sd = std::sqrt(stationary_var * -std::expm1(-2 * theta * dt));
  const double terminal_gain = -std::expm1(-theta * r.horizon);
  // Anchor near x0 for tiny relaxation, and near mu after substantial decay.
  // This avoids cancellation at both ends of the exact mean transition.
  const double terminal_mean = terminal_gain < 0.5 ? x0 + (mu - x0) * terminal_gain
                                                   : mu + (x0 - mu) * std::exp(-theta * r.horizon);
  const double terminal_var = stationary_var * -std::expm1(-2 * theta * r.horizon);
  Rng rng(r.seed);
  Moments stats;
  std::vector<double> samples;
  samples.reserve(r.paths);
  for (std::size_t p = 0; p < r.paths; ++p) {
    double x = x0;
    if (p < 24) {
      std::vector<double> t(r.steps + 1), y(r.steps + 1);
      y[0] = x0;
      for (std::size_t k = 1; k <= r.steps; ++k) {
        x = (gain < 0.5 ? x + (mu - x) * gain : mu + (x - mu) * decay) +
            transition_sd * rng.normal();
        t[k] = dt * static_cast<double>(k);
        y[k] = x;
      }
      t.back() = r.horizon;
      out["series"].push_back({{"t", t}, {"y", y}, {"hit", nullptr}});
    } else
      x = terminal_mean + std::sqrt(terminal_var) * rng.normal();
    stats.add(x);
    samples.push_back(x);
  }
  out["algorithm"] = "Exact Ornstein–Uhlenbeck Gaussian transitions; exact terminal marginal for "
                     "aggregate samples";
  out["histogram"] = histogram(samples);
  moment_metrics(out, stats, terminal_mean, terminal_var);
  out["metrics"].push_back(
      compared("stationary_mean", "Mean vs stationary limit", stats.mean, mu, stats.se(),
               "Finite-horizon observations are compared with the stationary limit."));
  out["metrics"].push_back(compared(
      "stationary_variance", "Variance vs stationary limit", stats.variance(), stationary_var, -1,
      "Stationary variance is σ² / (2θ); initial condition is deterministic."));
  if (theta * r.horizon < 5)
    out["warnings"].push_back("The horizon is less than five relaxation times; terminal moments "
                              "may differ materially from stationarity.");
}

void check_event_budget(double expected) {
  if (!std::isfinite(expected) || expected > 0.8 * static_cast<double>(max_events))
    throw std::invalid_argument(
        "Expected event work exceeds 4,000,000; reduce paths, rate, or horizon");
}

double event_time(double time, double wait) {
  const double next = time + wait;
  if (!(next > time))
    throw std::invalid_argument(
        "Event spacing exhausted floating-point time precision; reduce rate or horizon");
  return next;
}

void check_actual_events(std::uint64_t total, std::size_t visible, bool save) {
  if (total > max_events)
    throw std::invalid_argument("Simulation exceeded the 5,000,000 event safety cap");
  if (save && visible > max_visible_events)
    throw std::invalid_argument("A rendered path exceeded 12,000 events; reduce rate or horizon");
}

void poisson(const json &j, const Request &r, json &out) {
  const bool compound = r.model == "compound-poisson";
  const double rate = bounded(j, "lambda", 3, 0, 10000);
  const double mark_mean = bounded(j, "jump_mean", 1, -1e6, 1e6);
  const double mark_sd = bounded(j, "jump_sigma", 0.5, 0, 10000);
  check_event_budget(static_cast<double>(r.paths) * rate * r.horizon);
  Rng rng(r.seed);
  Moments stats, counts;
  std::vector<double> samples;
  samples.reserve(r.paths);
  std::uint64_t total_events = 0;
  for (std::size_t p = 0; p < r.paths; ++p) {
    const bool save = p < 8;
    double time = 0, value = 0;
    std::size_t count = 0;
    std::vector<double> t{0}, y{0}, events;
    if (rate > 0) {
      for (;;) {
        time = event_time(time, rng.exponential(rate));
        if (time >= r.horizon)
          break;
        ++total_events;
        ++count;
        check_actual_events(total_events, count, save);
        if (save) {
          t.push_back(time);
          y.push_back(value);
        }
        value += compound ? mark_mean + mark_sd * rng.normal() : 1;
        if (save) {
          t.push_back(time);
          y.push_back(value);
          events.push_back(time);
        }
      }
    }
    if (save) {
      t.push_back(r.horizon);
      y.push_back(value);
      out["series"].push_back({{"t", t}, {"y", y}, {"events", events}, {"hit", nullptr}});
    }
    counts.add(static_cast<double>(count));
    stats.add(value);
    samples.push_back(value);
  }
  out["algorithm"] =
      compound ? "Exact exponential interarrival times with independent Gaussian jump marks"
               : "Exact exponential interarrival times";
  out["histogram"] = histogram(samples, !compound);
  const double mean_count = rate * r.horizon;
  moment_metrics(out, stats, compound ? mean_count * mark_mean : mean_count,
                 compound ? mean_count * (mark_sd * mark_sd + mark_mean * mark_mean) : mean_count);
  out["metrics"].push_back(
      compared("event_count", "Mean event count", counts.mean, mean_count, counts.se()));
  out["metrics"].push_back(
      metric("total_events", "Generated events", static_cast<double>(total_events)));
  if (compound)
    out["warnings"].push_back(
        "Jump marks are Gaussian and may be negative. The counting process remains nondecreasing.");
}

using Matrix = std::vector<std::vector<double>>;

Matrix generator(const json &j) {
  Matrix q{{-2, 2, 0}, {1, -3, 2}, {0, 1, -1}};
  if (j.contains("Q")) {
    const auto &input = j["Q"];
    if (!input.is_array() || input.empty() || input.size() > 16)
      throw std::invalid_argument("Q must be a square matrix with 1 to 16 states");
    q.assign(input.size(), std::vector<double>(input.size()));
    for (std::size_t a = 0; a < q.size(); ++a) {
      if (!input[a].is_array() || input[a].size() != q.size())
        throw std::invalid_argument("Q must be square");
      for (std::size_t b = 0; b < q.size(); ++b) {
        if (!input[a][b].is_number())
          throw std::invalid_argument("Q entries must be numbers");
        q[a][b] = input[a][b].get<double>();
      }
    }
  }
  for (std::size_t a = 0; a < q.size(); ++a) {
    double row = 0, magnitude = 0;
    for (std::size_t b = 0; b < q.size(); ++b) {
      const double value = q[a][b];
      if (!std::isfinite(value) || std::abs(value) > 10000)
        throw std::invalid_argument("Q entries must be finite with magnitude ≤ 10,000");
      if ((a != b && value < 0) || (a == b && value > 0))
        throw std::invalid_argument("Q needs nonnegative off-diagonals and nonpositive diagonals");
      row += value;
      magnitude += std::abs(value);
    }
    if (std::abs(row) > 1e-10 * magnitude)
      throw std::invalid_argument("Every Q row must sum to zero");
    // Normalize within roundoff to make the hazard agree exactly with routing.
    double exit_rate = 0;
    for (std::size_t b = 0; b < q.size(); ++b)
      if (a != b)
        exit_rate += q[a][b];
    q[a][a] = -exit_rate;
  }
  return q;
}

std::vector<double> stationary(const Matrix &q) {
  const std::size_t n = q.size();
  Matrix a(n, std::vector<double>(n + 1));
  double scale = 0;
  for (const auto &row : q)
    for (double x : row)
      scale = std::max(scale, std::abs(x));
  if (scale == 0)
    return n == 1 ? std::vector<double>{1.0} : std::vector<double>{};
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t k = 0; k < n; ++k)
      a[i][k] = i + 1 == n ? 1 : q[k][i] / scale;
    a[i][n] = i + 1 == n ? 1 : 0;
  }
  for (std::size_t c = 0; c < n; ++c) {
    std::size_t pivot = c;
    for (std::size_t i = c + 1; i < n; ++i)
      if (std::abs(a[i][c]) > std::abs(a[pivot][c]))
        pivot = i;
    if (std::abs(a[pivot][c]) < 1e-12)
      return {};
    std::swap(a[c], a[pivot]);
    const double divisor = a[c][c];
    for (std::size_t k = c; k <= n; ++k)
      a[c][k] /= divisor;
    for (std::size_t i = 0; i < n; ++i) {
      if (i == c)
        continue;
      const double factor = a[i][c];
      for (std::size_t k = c; k <= n; ++k)
        a[i][k] -= factor * a[c][k];
    }
  }
  std::vector<double> result(n);
  for (std::size_t i = 0; i < n; ++i) {
    if (!std::isfinite(a[i][n]) || a[i][n] < -1e-9)
      return {};
    result[i] = std::max(0.0, a[i][n]);
  }
  const double sum = std::accumulate(result.begin(), result.end(), 0.0);
  if (!(sum > 0))
    return {};
  for (double &x : result)
    x /= sum;
  for (std::size_t k = 0; k < n; ++k) {
    double residual = 0;
    for (std::size_t i = 0; i < n; ++i)
      residual += result[i] * (q[i][k] / scale);
    if (std::abs(residual) > 1e-9)
      return {};
  }
  return result;
}

void ctmc(const json &j, const Request &r, json &out) {
  const Matrix q = generator(j);
  const std::size_t n = q.size();
  const auto initial = static_cast<std::size_t>(integer(j, "x0", 0, 0, n - 1));
  double max_rate = 0;
  for (std::size_t i = 0; i < n; ++i)
    max_rate = std::max(max_rate, -q[i][i]);
  check_event_budget(static_cast<double>(r.paths) * r.horizon * max_rate);
  const auto theoretical = stationary(q);
  std::vector<double> occupation(n, 0), samples;
  samples.reserve(r.paths);
  Rng rng(r.seed);
  Moments transitions;
  std::uint64_t total = 0;
  for (std::size_t p = 0; p < r.paths; ++p) {
    const bool save = p < 8;
    std::size_t state = initial, count = 0;
    double time = 0;
    std::vector<double> t{0}, y{static_cast<double>(state)}, events;
    while (time < r.horizon) {
      const double rate = -q[state][state];
      const double next = rate > 0 ? event_time(time, rng.exponential(rate)) : r.horizon;
      occupation[state] += std::min(next, r.horizon) - time;
      if (next >= r.horizon)
        break;
      time = next;
      ++total;
      ++count;
      check_actual_events(total, count, save);
      if (save) {
        t.push_back(time);
        y.push_back(static_cast<double>(state));
      }
      const double draw = rng.uniform() * rate;
      double cumulative = 0;
      std::size_t destination = state;
      for (std::size_t b = 0; b < n; ++b) {
        if (b == state || q[state][b] == 0)
          continue;
        destination = b;
        cumulative += q[state][b];
        if (draw < cumulative)
          break;
      }
      state = destination;
      if (save) {
        t.push_back(time);
        y.push_back(static_cast<double>(state));
        events.push_back(time);
      }
    }
    if (save) {
      t.push_back(r.horizon);
      y.push_back(static_cast<double>(state));
      out["series"].push_back({{"t", t}, {"y", y}, {"events", events}, {"hit", nullptr}});
    }
    transitions.add(static_cast<double>(count));
    samples.push_back(static_cast<double>(state));
  }
  for (double &x : occupation)
    x /= r.horizon * static_cast<double>(r.paths);
  out["algorithm"] =
      "Exact CTMC holding times and generator-weighted jumps; pivoted stationary linear solve";
  out["occupation"] = occupation;
  out["stationary"] = theoretical;
  out["Q"] = q;
  out["histogram"] = histogram(samples, true);
  out["metrics"].push_back(metric("event_count", "Mean state transitions", transitions.mean));
  out["metrics"].push_back(
      metric("total_events", "Generated transitions", static_cast<double>(total)));
  if (theoretical.empty())
    out["warnings"].push_back(
        "The stationary system is singular or ill-conditioned: no unique stationary comparison is "
        "reported. Reducible finite CTMCs may have multiple stationary distributions.");
  else {
    double error = 0;
    for (std::size_t i = 0; i < n; ++i) {
      error += std::abs(occupation[i] - theoretical[i]);
      json m = compared(("occupation_" + std::to_string(i)).c_str(),
                        ("Occupation: state " + std::to_string(i)).c_str(), occupation[i],
                        theoretical[i]);
      out["metrics"].push_back(std::move(m));
    }
    out["metrics"].push_back(compared("stationary_error", "Occupation L¹ error", error, 0));
    out["warnings"].push_back("Occupation includes the initial transient; stationary agreement "
                              "requires a horizon long relative to mixing time.");
  }
}

double hawkes_expected_count(double mu, double alpha, double beta, double horizon) {
  const double d = beta - alpha;
  const double x = d * horizon;
  const double integral = std::abs(x) < 1e-3
                              ? horizon * (x / 2 - x * x / 6 + x * x * x / 24 - x * x * x * x / 120)
                              : horizon + std::expm1(-x) / d;
  return mu * horizon + mu * alpha / d * integral;
}

json hawkes_series(const std::vector<double> &events, const Request &r, double mu, double alpha,
                   double beta) {
  std::vector<double> t{0}, y{0}, intensity{mu};
  std::size_t grid = 1, event = 0;
  double time = 0, excitation = 0;
  while (grid <= r.steps || event < events.size()) {
    const double grid_time =
        grid <= r.steps ? r.horizon * static_cast<double>(grid) / static_cast<double>(r.steps)
                        : std::numeric_limits<double>::infinity();
    const double event_time =
        event < events.size() ? events[event] : std::numeric_limits<double>::infinity();
    const double next = std::min(grid_time, event_time);
    excitation *= std::exp(-beta * (next - time));
    time = next;
    t.push_back(time);
    y.push_back(static_cast<double>(event));
    intensity.push_back(mu + excitation);
    if (event_time <= grid_time) {
      excitation += alpha;
      ++event;
      t.push_back(time);
      y.push_back(static_cast<double>(event));
      intensity.push_back(mu + excitation);
    } else
      ++grid;
  }
  t.back() = r.horizon;
  return {{"t", t}, {"y", y}, {"intensity", intensity}, {"events", events}, {"hit", nullptr}};
}

void hawkes(const json &j, const Request &r, json &out) {
  const double mu = bounded(j, "mu", 0.5, 0, 10000);
  const double alpha = bounded(j, "alpha", 0.7, 0, 10000);
  const double beta = bounded(j, "beta", 1.2, 1e-6, 10000);
  if (alpha >= beta)
    throw std::invalid_argument("Hawkes requires alpha < beta (branching ratio below one)");
  const double ratio = alpha / beta;
  if (ratio > 0.9)
    out["warnings"].push_back("Near-critical Hawkes parameters can produce large clusters and high "
                              "Monte Carlo uncertainty.");
  const double expected = hawkes_expected_count(mu, alpha, beta, r.horizon);
  check_event_budget(static_cast<double>(r.paths) * expected);
  Rng rng(r.seed);
  Moments counts, terminal_intensity;
  std::vector<double> samples;
  samples.reserve(r.paths);
  std::uint64_t total = 0, proposals = 0;
  for (std::size_t p = 0; p < r.paths; ++p) {
    const bool save = p < 8;
    std::vector<double> events;
    double time = 0, excitation = 0;
    std::size_t count = 0;
    for (;;) {
      const double bound = mu + excitation;
      if (bound == 0)
        break;
      const double wait = rng.exponential(bound);
      const double next = event_time(time, wait);
      if (next >= r.horizon) {
        excitation *= std::exp(-beta * (r.horizon - time));
        break;
      }
      time = next;
      excitation *= std::exp(-beta * wait);
      if (++proposals > 20'000'000)
        throw std::invalid_argument("Hawkes thinning exceeded the 20,000,000 proposal safety cap");
      if (rng.uniform() * bound <= mu + excitation) {
        excitation += alpha;
        ++total;
        ++count;
        check_actual_events(total, count, save);
        if (save)
          events.push_back(time);
      }
    }
    counts.add(static_cast<double>(count));
    terminal_intensity.add(mu + excitation);
    samples.push_back(static_cast<double>(count));
    if (save)
      out["series"].push_back(hawkes_series(events, r, mu, alpha, beta));
  }
  out["algorithm"] =
      "Ogata adaptive thinning for an exponential Hawkes kernel; empty event history at t = 0";
  out["histogram"] = histogram(samples, true);
  out["metrics"].push_back(
      compared("mean", "Mean event count", counts.mean, expected, counts.se()));
  out["metrics"].push_back(metric("variance", "Count variance", counts.variance()));
  out["metrics"].push_back(
      compared("terminal_intensity", "Mean terminal intensity", terminal_intensity.mean,
               mu + mu * alpha / (beta - alpha) * -std::expm1(-(beta - alpha) * r.horizon),
               terminal_intensity.se()));
  out["diagnostics"] = json::array();
  out["diagnostics"].push_back(metric("branching_ratio", "Branching ratio α / β", ratio));
  out["diagnostics"].push_back(
      metric("stationary_intensity", "Stationary mean intensity", mu / (1 - ratio)));
  out["metrics"].push_back(metric("total_events", "Generated events", static_cast<double>(total)));
  out["warnings"].push_back("Simulation starts with no past events. Count theory accounts for this "
                            "transient; stationary intensity is a long-run limit.");
}

} // namespace

json simulate(const json &request) {
  const auto start = std::chrono::steady_clock::now();
  const Request r(request);
  json out = r.response();
  if (r.model == "brownian" || r.model == "first-passage" || r.model == "reflection" ||
      r.model == "qv")
    brownian(request, r, out);
  else if (r.model == "random-walk")
    random_walk(request, r, out);
  else if (r.model == "ou" || r.model == "stationarity")
    ou(request, r, out);
  else if (r.model == "poisson" || r.model == "compound-poisson")
    poisson(request, r, out);
  else if (r.model == "ctmc")
    ctmc(request, r, out);
  else if (r.model == "hawkes")
    hawkes(request, r, out);
  else
    throw std::invalid_argument("Unknown model: " + r.model);
  if (r.paths == 1)
    out["warnings"].push_back("A single realization cannot estimate an across-path sample variance "
                              "or confidence interval.");
  out["elapsed_ms"] =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  return out;
}

} // namespace stochlab
