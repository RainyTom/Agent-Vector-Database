#include "vector_join/vector_join.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

void usage(const char* program) {
  std::cerr
      << "Usage: " << program << " --base FILE --queries FILE --dim N --threshold R "
      << "[--method independent|hard|soft|mi|mi-adapt] [options]\n"
      << "Threshold R is squared L2, matching datasets/*/meta.json.\n"
      << "Options: --base-limit N --query-limit N --ground-truth-limit N\n"
      << "         --m N --ef-construction N --greedy-beam N --hybrid-beam N\n"
      << "         --convergence N --ood-ratio X --seed N\n";
}

std::unordered_map<std::string, std::string> parse_args(int argc, char** argv) {
  std::unordered_map<std::string, std::string> args;
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    if (!key.starts_with("--") || i + 1 >= argc) {
      throw std::invalid_argument("expected --key value near: " + key);
    }
    args[key.substr(2)] = argv[++i];
  }
  return args;
}

std::string required(const std::unordered_map<std::string, std::string>& args,
                     const std::string& key) {
  const auto it = args.find(key);
  if (it == args.end()) throw std::invalid_argument("missing --" + key);
  return it->second;
}

std::size_t integer(const std::unordered_map<std::string, std::string>& args,
                    const std::string& key, std::size_t fallback) {
  const auto it = args.find(key);
  return it == args.end() ? fallback : std::stoull(it->second);
}

float real(const std::unordered_map<std::string, std::string>& args,
           const std::string& key, float fallback) {
  const auto it = args.find(key);
  return it == args.end() ? fallback : std::stof(it->second);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 1) {
      usage(argv[0]);
      return EXIT_FAILURE;
    }
    const auto args = parse_args(argc, argv);
    const std::size_t dim = std::stoull(required(args, "dim"));
    const auto base = vjoin::Matrix::load_raw(required(args, "base"), dim,
                                               integer(args, "base-limit", 0));
    const auto queries = vjoin::Matrix::load_raw(required(args, "queries"), dim,
                                                  integer(args, "query-limit", 0));

    vjoin::Config config;
    config.method = vjoin::parse_method(args.contains("method") ? args.at("method")
                                                                 : "independent");
    config.threshold = std::stof(required(args, "threshold"));
    config.graph_m = integer(args, "m", config.graph_m);
    config.ef_construction = integer(args, "ef-construction", config.ef_construction);
    config.greedy_beam = integer(args, "greedy-beam", config.greedy_beam);
    config.hybrid_beam = integer(args, "hybrid-beam", config.hybrid_beam);
    config.convergence_threshold = integer(args, "convergence", config.convergence_threshold);
    config.ood_ratio_threshold = real(args, "ood-ratio", config.ood_ratio_threshold);
    config.seed = static_cast<std::uint32_t>(integer(args, "seed", config.seed));

    const auto output = vjoin::run(queries, base, config,
                                   integer(args, "ground-truth-limit", 0));
    std::cout << vjoin::metrics_json(output.metrics) << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    usage(argv[0]);
    return EXIT_FAILURE;
  }
}
