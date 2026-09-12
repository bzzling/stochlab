#include "stochlab/processes.hpp"
#include "stochlab/rng.hpp"

#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace stochlab {
namespace {
void require(bool condition, const std::string &description) {
  if (!condition)
    throw std::runtime_error("process test failed: " + description);
}

void near(double actual, double expected, double tolerance, const std::string &label) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
    throw std::runtime_error(label + ": actual=" + std::to_string(actual) + " expected=" +
                             std::to_string(expected) + " tolerance=" + std::to_string(tolerance));
}

void invalid(const json &request, const std::string &label) {
  bool rejected = false;
  try {
    (void)simulate(request);
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "invalid request accepted: " + label);
}

json find_metric(const json &result, const std::string &key) {
  for (const auto &m : result.at("metrics"))
    if (m.at("key") == key)
      return m;
  throw std::runtime_error("missing metric: " + key);
}

double value(const json &result, const std::string &key) {
  return find_metric(result, key).at("value").get<double>();
}

void common_invariants(const json &result) {
  require(!result.at("algorithm").get<std::string>().empty(), "algorithm description");
  const auto &series = result.at("series");
  require(!series.empty() && series.size() <= 32, "bounded representative series");
  const double horizon = result.at("T").get<double>();
  for (const auto &path : series) {
    const auto t = path.at("t").get<std::vector<double>>();
    const auto y = path.at("y").get<std::vector<double>>();
    require(t.size() == y.size() && t.size() >= 2, "aligned time / value arrays");
    near(t.front(), 0, 0, "series starts at zero");
    near(t.back(), horizon, 0, "series ends exactly at horizon");
    for (std::size_t k = 0; k < t.size(); ++k) {
      require(std::isfinite(t[k]) && std::isfinite(y[k]), "finite series");
      if (k > 0)
        require(t[k] >= t[k - 1], "ordered time axis");
    }
    if (!path.at("hit").is_null())
      require(path.at("hit").get<std::size_t>() < t.size(), "valid hit index");
    if (path.contains("events")) {
      const auto events = path.at("events").get<std::vector<double>>();
      for (std::size_t k = 0; k < events.size(); ++k) {
        require(events[k] > 0 && events[k] < horizon, "events within open horizon");
        if (k > 0)
          require(events[k] > events[k - 1], "strictly ordered events");
      }
    }
    if (path.contains("intensity")) {
      const auto intensity = path.at("intensity").get<std::vector<double>>();
      require(intensity.size() == t.size(), "aligned intensity series");
      for (const double lambda : intensity)
        require(std::isfinite(lambda) && lambda >= 0, "finite nonnegative intensity");
    }
  }
  std::uint64_t observations = 0;
  for (const auto &bin : result.at("histogram")) {
    observations += bin.at("count").get<std::uint64_t>();
    require(std::isfinite(bin.at("density").get<double>()) && bin.at("density").get<double>() >= 0,
            "valid histogram density");
  }
  require(observations == result.at("paths").get<std::uint64_t>(),
          "histogram contains every realization");
  for (const auto &m : result.at("metrics")) {
    require(std::isfinite(m.at("value").get<double>()), "finite metric");
    if (m.contains("theory"))
      require(std::isfinite(m.at("theory").get<double>()), "finite theory");
    if (m.contains("se"))
      require(m.at("se").get<double>() >= 0, "nonnegative standard error");
  }
}

void report(const std::string &label, double empirical, double theory, double tolerance) {
  near(empirical, theory, tolerance, label);
  std::cout << "  PASS  " << std::left << std::setw(28) << label << std::right
            << " empirical=" << std::fixed << std::setprecision(6) << empirical
            << "  theory=" << theory << "  tolerance=";
  if (tolerance < 1e-6)
    std::cout << std::scientific;
  std::cout << tolerance << std::fixed << '\n';
}
} // namespace

void test_processes() {
  Rng known(0);
  require(known.next_u64() == UINT64_C(0xe220a8397b1dcdaf), "SplitMix64 reference vector");
  Rng a(9123), b(9123);
  for (int i = 0; i < 10000; ++i) {
    const double u = a.uniform();
    require(u > 0 && u < 1, "uniform excludes endpoints");
    require(u == b.uniform(), "deterministic uniform stream");
    const auto index = a.integer(7);
    require(index < 7 && index == b.integer(7), "bounded deterministic integer draw");
    require(a.normal() == b.normal(), "deterministic Gaussian draws");
    require(a.exponential(4) == b.exponential(4), "deterministic exponential draws");
  }

  const std::vector<std::string> models{
      "random-walk",  "brownian", "first-passage",    "reflection", "qv",    "ou",
      "stationarity", "poisson",  "compound-poisson", "ctmc",       "hawkes"};
  for (const auto &model : models) {
    const json input{{"model", model}, {"seed", 731}, {"paths", 48}, {"steps", 32}, {"T", 2}};
    auto first = simulate(input);
    auto second = simulate(input);
    common_invariants(first);
    first.erase("elapsed_ms");
    second.erase("elapsed_ms");
    require(first == second, model + " repeatability");
  }

  invalid({{"model", "unknown"}}, "unknown model");
  invalid({{"model", 2}}, "model type");
  invalid(json::array(), "request object");
  invalid({{"paths", 0}}, "empty simulation");
  invalid({{"paths", 4.5}}, "fractional count");
  invalid({{"paths", 1'000'001}}, "path resource bound");
  invalid({{"seed", -1}}, "negative seed");
  invalid({{"seed", 4294967296.0}}, "seed overflow");
  invalid({{"steps", 0}}, "empty partition");
  invalid({{"T", 0}}, "zero horizon");
  invalid({{"T", std::numeric_limits<double>::infinity()}}, "infinite horizon");
  invalid({{"T", "2"}}, "string horizon");
  invalid({{"barrier", -1}}, "unsupported lower barrier");
  invalid({{"model", "ou"}, {"theta", 0}}, "non-mean-reverting OU");
  invalid({{"model", "ou"}, {"sigma", -0.1}}, "negative volatility");
  invalid({{"model", "hawkes"}, {"alpha", 1.2}, {"beta", 1.2}}, "critical Hawkes");
  invalid({{"model", "hawkes"}, {"alpha", 1.3}, {"beta", 1.2}}, "unstable Hawkes");
  invalid({{"model", "hawkes"}, {"mu", -1}}, "negative baseline");
  invalid({{"model", "poisson"}, {"lambda", -1}}, "negative rate");
  invalid({{"model", "poisson"}, {"lambda", 1000}, {"paths", 10000}}, "event budget");
  invalid({{"model", "random-walk"}, {"paths", 1000000}, {"steps", 256}}, "increment budget");
  invalid({{"model", "ctmc"}, {"Q", {{-1, 2}, {1, -1}}}}, "Q row sums");
  invalid({{"model", "ctmc"}, {"Q", {{0, 1e-12}, {0, 0}}}}, "Q row sums at small scale");
  invalid({{"model", "ctmc"}, {"Q", {{1, -1}, {1, -1}}}}, "Q signs");
  invalid({{"model", "ctmc"}, {"Q", {{-1, 1}, {1}}}}, "Q dimensions");
  invalid({{"model", "ctmc"}, {"Q", {{0}}}, {"x0", 1}}, "CTMC initial state");

  const auto zero_barrier = simulate({{"model", "first-passage"}, {"barrier", 0}, {"paths", 100}});
  near(value(zero_barrier, "hit_probability"), 1, 0, "barrier at origin is hit at t=0");
  for (const auto &path : zero_barrier.at("series"))
    require(path["hit"] == 0, "grid hit at origin");

  const auto walk = simulate({{"model", "random-walk"}, {"T", 3}, {"steps", 12}, {"paths", 4}});
  for (const auto &path : walk.at("series")) {
    const auto y = path["y"].get<std::vector<double>>();
    for (std::size_t k = 1; k < y.size(); ++k)
      near(std::abs(y[k] - y[k - 1]), 0.5, 0, "Bernoulli increment size");
  }
  const auto deterministic_ou = simulate({{"model", "ou"},
                                          {"sigma", 0},
                                          {"mu", 2},
                                          {"x0", -1},
                                          {"theta", 3},
                                          {"T", 0.8},
                                          {"steps", 8},
                                          {"paths", 32}});
  near(value(deterministic_ou, "mean"), 2 - 3 * std::exp(-2.4), 1e-14,
       "zero-noise exact OU solution");
  near(value(deterministic_ou, "variance"), 0, 1e-27, "zero-noise OU variance");

  const auto tiny_relaxation = simulate({{"model", "ou"},
                                         {"sigma", 0},
                                         {"mu", 1e6},
                                         {"x0", 0},
                                         {"theta", 1e-6},
                                         {"T", 1e-6},
                                         {"steps", 8192},
                                         {"paths", 25}});
  const double tiny_exact = 1e6 * -std::expm1(-1e-12);
  near(tiny_relaxation["series"][0]["y"].back().get<double>(), tiny_exact, 2e-18,
       "tiny OU relaxation preserves deterministic drift on a fine grid");
  near(find_metric(tiny_relaxation, "mean")["theory"].get<double>(), tiny_exact, 1e-21,
       "tiny OU relaxation has accurate terminal theory");
  near(value(tiny_relaxation, "mean"), tiny_exact, 2e-18,
       "tiny OU relaxation agrees across displayed and aggregate endpoints");
  near(value(tiny_relaxation, "variance"), 0, 1e-34,
       "tiny deterministic OU relaxation has no spurious sample variance");
  const auto full_relaxation = simulate({{"model", "ou"},
                                         {"sigma", 0},
                                         {"mu", 1e-20},
                                         {"x0", 1e6},
                                         {"theta", 100},
                                         {"T", 1},
                                         {"steps", 1},
                                         {"paths", 25}});
  near(value(full_relaxation, "mean"), 1e-20, 1e-34,
       "complete OU relaxation preserves a small nonzero long-run mean");

  for (const auto &input :
       std::vector<json>{{{"model", "poisson"}, {"lambda", 0}}, {{"model", "hawkes"}, {"mu", 0}}}) {
    const auto zero = simulate(input);
    near(value(zero, "mean"), 0, 0, "zero-rate process");
    for (const auto &path : zero["series"])
      require(path["events"].empty(), "no zero-rate events");
  }
  const auto counted = simulate({{"model", "poisson"}, {"lambda", 5}, {"T", 3}, {"paths", 10}});
  for (const auto &path : counted["series"]) {
    const auto y = path["y"].get<std::vector<double>>();
    for (std::size_t k = 1; k < y.size(); ++k)
      require(y[k] - y[k - 1] == 0 || y[k] - y[k - 1] == 1, "unit Poisson jumps");
    near(y.back(), static_cast<double>(path["events"].size()), 0, "count equals event length");
  }
  const auto deterministic_marks = simulate(
      {{"model", "compound-poisson"}, {"jump_mean", -2}, {"jump_sigma", 0}, {"paths", 10}});
  for (const auto &path : deterministic_marks["series"])
    near(path["y"].back().get<double>(), -2 * static_cast<double>(path["events"].size()), 0,
         "compound marks multiply exact count");

  const auto singleton = simulate({{"model", "ctmc"}, {"Q", {{0}}}, {"paths", 10}});
  require(singleton["stationary"] == json::array({1.0}), "singleton stationary distribution");
  near(singleton["occupation"][0].get<double>(), 1, 1e-14, "absorbing occupation");
  const auto reducible = simulate({{"model", "ctmc"}, {"Q", {{0, 0}, {0, 0}}}, {"paths", 10}});
  require(reducible["stationary"].empty(), "non-unique stationary distribution flagged");
  require(!reducible["warnings"].empty(), "reducible warning");
  const auto absorbed =
      simulate({{"model", "ctmc"}, {"Q", {{0, 0}, {1, -1}}}, {"x0", 1}, {"paths", 10}, {"T", 20}});
  near(absorbed["stationary"][0].get<double>(), 1, 1e-12, "unique reducible stationary class");
  for (const auto &path : absorbed["series"])
    require(path["events"].size() <= 1, "one transition before absorption");
  const auto slow =
      simulate({{"model", "ctmc"}, {"Q", {{-2e-200, 2e-200}, {1e-200, -1e-200}}}, {"paths", 2}});
  near(slow["stationary"][0].get<double>(), 1.0 / 3, 1e-12,
       "stationary solution is invariant to time scale");

  const auto hawkes_result = simulate(
      {{"model", "hawkes"}, {"paths", 10}, {"mu", 0.8}, {"alpha", 0.6}, {"beta", 1.3}, {"T", 8}});
  for (const auto &path : hawkes_result["series"]) {
    const auto t = path["t"].get<std::vector<double>>();
    const auto intensity = path["intensity"].get<std::vector<double>>();
    for (std::size_t k = 1; k < t.size(); ++k) {
      if (t[k] == t[k - 1])
        near(intensity[k] - intensity[k - 1], 0.6, 1e-12, "Hawkes excitation jump");
      else
        near(intensity[k] - 0.8, (intensity[k - 1] - 0.8) * std::exp(-1.3 * (t[k] - t[k - 1])),
             1e-12, "Hawkes exponential decay");
    }
  }

  const auto qv = simulate({{"model", "qv"}, {"steps", 96}, {"paths", 1}, {"T", 2}});
  const auto y = qv["series"][0]["y"].get<std::vector<double>>();
  for (const auto &entry : qv["qv"]) {
    const auto n = entry["partitions"].get<std::size_t>();
    require(96 % n == 0, "nested partition divides rendered grid");
    double actual = 0;
    const std::size_t stride = 96 / n;
    for (std::size_t k = stride; k <= 96; k += stride)
      actual += std::pow(y[k] - y[k - stride], 2);
    near(entry["value"].get<double>(), actual, 1e-12, "QV is sum of displayed increments squared");
  }
  std::cout
      << "  PASS  process invariants, seeded repeatability, invalid inputs, exact transitions\n";
}

void validate_processes() {
  std::cout << "STOCHLAB / SEEDED STATISTICAL VALIDATION\n";
  const double n_bm = 120000;
  const auto bm = simulate({{"model", "first-passage"},
                            {"seed", 831},
                            {"paths", n_bm},
                            {"steps", 128},
                            {"T", 1.5},
                            {"barrier", 1}});
  report("Brownian terminal mean", value(bm, "mean"), 0, 5 * std::sqrt(1.5 / n_bm));
  report("Brownian terminal variance", value(bm, "variance"), 1.5,
         5 * 1.5 * std::sqrt(2 / (n_bm - 1)));
  const double p_hit = std::erfc(1 / std::sqrt(3.0));
  report("Continuous first passage", value(bm, "hit_probability"), p_hit,
         5 * std::sqrt(p_hit * (1 - p_hit) / n_bm));
  report("Brownian running maximum", value(bm, "max_mean"), std::sqrt(3 / std::acos(-1.0)),
         5 * std::sqrt(1.5 * (1 - 2 / std::acos(-1.0)) / n_bm));

  const auto qv =
      simulate({{"model", "qv"}, {"seed", 201}, {"paths", 50000}, {"steps", 1024}, {"T", 2}});
  report("Quadratic variation mean", value(qv, "qv_mean"), 2, 5 * std::sqrt(8.0 / (1024 * 50000)));
  report("Quadratic variation variance", value(qv, "qv_variance"), 8.0 / 1024,
         6 * (8.0 / 1024) * std::sqrt((2 + 12.0 / 1024) / 49999));

  const auto walk = simulate(
      {{"model", "random-walk"}, {"seed", 204}, {"paths", 40000}, {"steps", 256}, {"T", 1}});
  report("Scaled random walk mean", value(walk, "mean"), 0, 5 / std::sqrt(40000.0));
  report("Scaled random walk variance", value(walk, "variance"), 1, 5 * std::sqrt(2.0 / 39999));

  const auto ou_result = simulate({{"model", "stationarity"},
                                   {"seed", 909},
                                   {"paths", 100000},
                                   {"theta", 1.7},
                                   {"mu", -0.4},
                                   {"sigma", 0.8},
                                   {"x0", 2},
                                   {"T", 10}});
  const double ou_var = 0.64 / 3.4;
  report("OU stationary mean", value(ou_result, "mean"), -0.4, 5 * std::sqrt(ou_var / 100000));
  report("OU stationary variance", value(ou_result, "variance"), ou_var,
         5 * ou_var * std::sqrt(2.0 / 99999));

  const auto pois =
      simulate({{"model", "poisson"}, {"seed", 812}, {"paths", 60000}, {"lambda", 3.5}, {"T", 2}});
  report("Poisson count mean", value(pois, "mean"), 7, 5 * std::sqrt(7.0 / 60000));
  report("Poisson count variance", value(pois, "variance"), 7,
         5 * std::sqrt((7 + 2 * 49.0) / 59999));

  const auto compound = simulate({{"model", "compound-poisson"},
                                  {"seed", 618},
                                  {"paths", 80000},
                                  {"lambda", 2.3},
                                  {"T", 1.7},
                                  {"jump_mean", 0.4},
                                  {"jump_sigma", 0.7}});
  const double cp_var = 2.3 * 1.7 * (0.16 + 0.49);
  report("Compound Poisson mean", value(compound, "mean"), 2.3 * 1.7 * 0.4,
         5 * std::sqrt(cp_var / 80000));
  const double mark_fourth = std::pow(0.4, 4) + 6 * 0.16 * 0.49 + 3 * 0.49 * 0.49;
  report("Compound Poisson variance", value(compound, "variance"), cp_var,
         5 * std::sqrt((2.3 * 1.7 * mark_fourth + 2 * cp_var * cp_var) / 79999));

  const auto chain = simulate({{"model", "ctmc"},
                               {"seed", 811},
                               {"paths", 1000},
                               {"T", 100},
                               {"Q", {{-2, 2}, {1, -1}}},
                               {"x0", 0}});
  report("CTMC stationary solve π₀", chain["stationary"][0].get<double>(), 1.0 / 3, 1e-12);
  // Exact finite-horizon occupation expectation includes the initial transient.
  const double expected_occ = 1.0 / 3 + 2.0 / 9 * (1 - std::exp(-300.0)) / 100;
  report("CTMC occupation proportion", chain["occupation"][0].get<double>(), expected_occ, 0.007);
  const auto occ = chain["occupation"].get<std::vector<double>>();
  near(std::accumulate(occ.begin(), occ.end(), 0.0), 1, 1e-12, "CTMC occupation conservation");

  const auto hk = simulate({{"model", "hawkes"},
                            {"seed", 809},
                            {"paths", 30000},
                            {"T", 8},
                            {"mu", 0.6},
                            {"alpha", 0.7},
                            {"beta", 1.3}});
  const double expected_count =
      0.6 * 1.3 / 0.6 * 8 - 0.6 * 0.7 / (0.6 * 0.6) * (1 - std::exp(-4.8));
  report("Hawkes transient count", value(hk, "mean"), expected_count,
         6 * find_metric(hk, "mean")["se"].get<double>());
  report("Hawkes terminal intensity", value(hk, "terminal_intensity"), 1.3 - 0.7 * std::exp(-4.8),
         6 * find_metric(hk, "terminal_intensity")["se"].get<double>());
  require(value(hk, "variance") > value(hk, "mean"),
          "Hawkes clustering produces overdispersed counts in this test");
  std::cout << "All statistical checks passed. Fixed seeds; tolerances use 5–6 sampling standard "
               "errors.\n";
}

} // namespace stochlab
