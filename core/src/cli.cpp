#include "stochlab/api.hpp"
#include "stochlab/exchange.hpp"
#include "stochlab/processes.hpp"
#include "stochlab/rng.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using stochlab::json;
using Clock = std::chrono::steady_clock;
namespace {
void help() {
  std::cout << R"(STOCHLAB / stochastic systems workbench

  stochlab simulate MODEL [options]
  stochlab experiment EXPERIMENT [options]
  stochlab exchange [options]
  stochlab bench [--json]

Models      random-walk, brownian, ou, poisson, compound-poisson, ctmc, hawkes
Experiments first-passage, reflection, qv, stationarity

Shared      --seed 42 --paths 10000 --steps 512 --T 1 --json
Brownian    --barrier 1
OU          --theta 2 --mu 0 --sigma 1 --x0 0
Poisson     --lambda 5 --jump_mean 0 --jump_sigma 1
CTMC        --Q '[[-2,2],[1,-1]]' --x0 0
Hawkes      --mu 0.8 --alpha 0.7 --beta 1.2
Exchange    --events 100000 --flow poisson|hawkes --maker on|off
            --rate 20 --skew 0.05 --seed 42

Examples
  stochlab experiment first-passage --paths 1000000 --barrier 1 --seed 42
  stochlab simulate hawkes --T 50 --paths 1000 --mu .8 --alpha .7 --beta 1.2
  stochlab exchange --events 100000 --flow hawkes --maker on

JSON output is the same bounded result schema used by the WASM interface.
)";
}
double number(const std::string &text) {
  std::size_t end = 0;
  double n = std::stod(text, &end);
  if (end != text.size() || !std::isfinite(n))
    throw std::invalid_argument("Not a finite number: " + text);
  return n;
}
json benchmark() {
  json rows = json::array();
  double sink = 0;
  auto measure = [&](const std::string &name, int work, const std::string &unit, auto fn) {
    std::vector<double> timings;
    for (int rep = 0; rep < 4; ++rep) {
      const auto begin = Clock::now();
      sink += fn();
      const double ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
      if (rep > 0)
        timings.push_back(ms);
    }
    std::sort(timings.begin(), timings.end());
    const double median = timings[1];
    rows.push_back({{"name", name},
                    {"work", work},
                    {"unit", unit},
                    {"median_ms", median},
                    {"per_second", work * 1000.0 / median}});
  };
  measure("Brownian grid generation", 10240000, "increments", [&]() {
    stochlab::Rng rng(42);
    double total = 0;
    for (int i = 0; i < 10000; ++i) {
      double x = 0;
      for (int j = 0; j < 1024; ++j)
        x += rng.normal() / 32;
      total += x;
    }
    return total;
  });
  measure("First passage (exact maximum)", 1000000, "paths", [&]() {
    const auto r =
        stochlab::simulate({{"model", "first-passage"}, {"paths", 1000000}, {"seed", 42}});
    return r["metrics"][0]["value"].template get<double>();
  });
  for (const auto &model : {"poisson", "hawkes", "ctmc"}) {
    measure(std::string(model) + " event simulation", 1000, "realizations", [&]() {
      const auto r =
          stochlab::simulate({{"model", model}, {"paths", 1000}, {"T", 20}, {"seed", 42}});
      return r["metrics"][0]["value"].template get<double>();
    });
  }
  for (const auto &flow : {"poisson", "hawkes"}) {
    measure(std::string("Exchange / ") + flow, 100000, "events", [&]() {
      stochlab::Exchange exchange({{"seed", 42}, {"flow", flow}, {"maker", true}});
      return exchange.advance(100000)["mid"].template get<double>();
    });
  }
  return {{"benchmarks", rows},
          {"checksum", sink},
          {"method", "1 warmup, median of 3 measured runs; seed 42; serialization excluded"}};
}
void print_metrics(const json &result) {
  std::cout << std::fixed;
  std::cout << "\nSTOCHLAB / " << result.value("model", std::string("simulation")) << "\n\n";
  for (auto key : {"paths", "steps", "T", "seed"}) {
    if (result.contains(key))
      std::cout << std::left << std::setw(22) << key << result[key].dump() << '\n';
  }
  std::cout << '\n'
            << std::left << std::setw(30) << "QUANTITY" << std::right << std::setw(16)
            << "EMPIRICAL" << std::setw(16) << "THEORY" << '\n';
  for (const auto &m : result["metrics"]) {
    std::cout << std::left << std::setw(30) << m["label"].get<std::string>() << std::right
              << std::setw(16) << std::setprecision(6) << m["value"].get<double>();
    if (m.contains("theory"))
      std::cout << std::setw(16) << m["theory"].get<double>();
    else
      std::cout << std::setw(16) << "—";
    std::cout << '\n';
    if (m.contains("se"))
      std::cout << "  standard error      " << m["se"].get<double>() << '\n';
  }
  for (const auto &d : result.value("diagnostics", json::array()))
    std::cout << "reference / " << d["label"].get<std::string>() << "  " << d["value"].get<double>()
              << '\n';
  std::cout << "\nalgorithm             " << result.value("algorithm", std::string()) << '\n';
  std::cout << "elapsed               " << std::setprecision(2) << result.value("elapsed_ms", 0.0)
            << " ms\n";
  for (const auto &warning : result["warnings"])
    std::cout << "note                  " << warning.get<std::string>() << '\n';
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "help") {
      help();
      return 0;
    }
    const std::string command = argv[1];
    json params = json::object();
    int start = 2;
    if (command == "simulate" || command == "experiment") {
      if (argc < 3)
        throw std::invalid_argument("A model/experiment is required. See --help.");
      params["model"] = argv[2];
      start = 3;
    } else if (command != "exchange" && command != "bench")
      throw std::invalid_argument("Unknown command: " + command);
    const std::set<std::string> numeric = {
        "seed",   "paths", "steps", "T",         "barrier",    "theta",  "mu",   "sigma", "x0",
        "lambda", "alpha", "beta",  "jump_mean", "jump_sigma", "events", "rate", "skew"};
    bool as_json = false;
    for (int i = start; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--json") {
        as_json = true;
        continue;
      }
      if (arg == "--help") {
        help();
        return 0;
      }
      if (!arg.starts_with("--"))
        throw std::invalid_argument("Expected option, got: " + arg);
      arg = arg.substr(2);
      if (!numeric.contains(arg) && arg != "flow" && arg != "maker" && arg != "Q")
        throw std::invalid_argument("Unknown option: --" + arg);
      if (i + 1 >= argc)
        throw std::invalid_argument("Missing value for --" + arg);
      const std::string value = argv[++i];
      if (numeric.contains(arg))
        params[arg] = number(value);
      else if (arg == "Q")
        params[arg] = json::parse(value);
      else if (arg == "maker") {
        if (value != "on" && value != "off")
          throw std::invalid_argument("--maker expects on or off");
        params[arg] = value == "on";
      } else
        params[arg] = value;
    }
    if (command == "bench") {
      if (!params.empty())
        throw std::invalid_argument("bench accepts only --json");
      const auto r = benchmark();
      if (as_json)
        std::cout << r.dump() << '\n';
      else {
        std::cout << "\nSTOCHLAB / NATIVE BENCHMARK\n\n"
                  << r["method"].get<std::string>() << "\n\n";
        for (const auto &row : r["benchmarks"])
          std::cout << std::left << std::setw(34) << row["name"].get<std::string>() << std::right
                    << std::fixed << std::setprecision(2) << std::setw(10)
                    << row["median_ms"].get<double>() << " ms   " << row["work"] << ' '
                    << row["unit"].get<std::string>() << '\n';
      }
    } else if (command == "exchange") {
      const double events = params.value("events", 10000.0);
      if (events < 1 || events > 1000000 || events != std::floor(events))
        throw std::invalid_argument("events must be an integer in [1,1000000]");
      stochlab::Exchange exchange(params);
      const auto begin = Clock::now();
      int remaining = static_cast<int>(events);
      while (remaining > 250000) {
        exchange.advance(250000);
        remaining -= 250000;
      }
      const auto r = exchange.advance(remaining);
      if (as_json)
        std::cout << r.dump() << '\n';
      else {
        std::cout << "\nSTOCHLAB / SYNTHETIC EXCHANGE\n\n";
        for (auto key : {"events", "time", "mid", "spread", "imbalance", "intensity"})
          std::cout << std::left << std::setw(22) << key << r[key] << '\n';
        std::cout << "\nMARKET MAKER\n";
        for (auto key : {"inventory", "cash", "pnl", "fills", "max_inventory", "spread_capture"})
          std::cout << std::left << std::setw(22) << key << r["maker"][key] << '\n';
        std::cout << "\nelapsed               "
                  << std::chrono::duration<double, std::milli>(Clock::now() - begin).count()
                  << " ms\n";
        std::cout
            << "Synthetic order flow; no fees or latency. This is not a model of a real market.\n";
      }
    } else {
      const auto r = stochlab::simulate(params);
      if (as_json)
        std::cout << r.dump() << '\n';
      else
        print_metrics(r);
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "stochlab: " << e.what() << '\n';
    return 2;
  }
}
