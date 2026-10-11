#include "apex/matcher.hpp"
#include "apex/types.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void usage() {
  std::cerr
      << "apex bench [--symbols N] [--orders N] [--threads N] [--seed N] [--span N]\n"
      << "apex export --output FILE [--symbols N] [--orders N] [--seed N] [--span N]\n"
      << "\n"
      << "bench matches a synthetic order stream in memory.\n"
      << "export writes fills as CSV that Strata can ingest with profile/apex-trades.toml.\n";
}

apex::Config parse_config(int argc, char** argv, int start, std::string* output) {
  apex::Config config;
  for (int i = start; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + arg);
      }
      return std::string(argv[++i]);
    };
    if (arg == "--symbols") {
      config.symbols = static_cast<uint32_t>(std::stoul(next()));
    } else if (arg == "--orders") {
      config.orders = std::stoull(next());
    } else if (arg == "--threads") {
      config.threads = static_cast<uint32_t>(std::stoul(next()));
    } else if (arg == "--seed") {
      config.seed = static_cast<uint32_t>(std::stoul(next()));
    } else if (arg == "--span") {
      config.span = static_cast<int32_t>(std::stol(next()));
    } else if (arg == "--output") {
      if (output == nullptr) {
        throw std::runtime_error("--output is only valid for export");
      }
      *output = next();
    } else {
      throw std::runtime_error("unknown argument " + arg);
    }
  }
  return config;
}

void print_rate(const char* label, double count, double seconds) {
  const double rate = seconds > 0.0 ? count / seconds : 0.0;
  std::cout << label << rate << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      usage();
      return 2;
    }
    const std::string command = argv[1];
    if (command == "--help" || command == "help") {
      usage();
      return 0;
    }
    if (command == "bench") {
      const apex::Config config = parse_config(argc, argv, 2, nullptr);
      const apex::RunResult result = apex::run_benchmark(config);
      std::cout << "threads " << result.threads << "\n";
      std::cout << "commands " << result.commands << "\n";
      std::cout << "trades " << result.stats.trades << "\n";
      std::cout << "filled_qty " << result.stats.filled_qty << "\n";
      std::cout << "cancels " << result.stats.cancels << "\n";
      std::cout << "misses " << result.stats.misses << "\n";
      std::cout << "expired " << result.stats.expired << "\n";
      std::cout << "generate_seconds " << result.generate_seconds << "\n";
      std::cout << "match_seconds " << result.match_seconds << "\n";
      print_rate("commands_per_sec ", static_cast<double>(result.commands), result.match_seconds);
      print_rate("trades_per_sec ", static_cast<double>(result.stats.trades), result.match_seconds);
      return 0;
    }
    if (command == "export") {
      std::string output;
      apex::Config config = parse_config(argc, argv, 2, &output);
      if (output.empty()) {
        throw std::runtime_error("export requires --output");
      }
      config.threads = 1;
      if (config.orders == 16000000) {
        config.orders = 100000;
      }
      const std::vector<apex::Command> commands = apex::generate_shard(0, 1, config);
      std::vector<apex::Trade> trades;
      const apex::Stats stats = apex::run_commands(config.symbols, commands, &trades);
      apex::write_trades_csv(output, trades);
      std::cout << "commands " << commands.size() << "\n";
      std::cout << "trades " << stats.trades << "\n";
      std::cout << "wrote " << trades.size() << " rows to " << output << "\n";
      return 0;
    }
    usage();
    return 2;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
}
