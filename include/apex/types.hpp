#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace apex {

inline constexpr uint32_t kNil = 0xffffffffu;
inline constexpr uint8_t kBuy = 0;
inline constexpr uint8_t kSell = 1;
inline constexpr uint32_t kMaxRestingPerSymbol = 64;

enum class Op : uint8_t { NewLimit = 1, NewMarket = 2, Cancel = 3 };

struct Command {
  uint64_t id = 0;
  uint32_t symbol = 0;
  int32_t price = 0;
  uint32_t qty = 0;
  uint8_t side = kBuy;
  Op op = Op::NewLimit;
};

struct Trade {
  uint64_t taker_id = 0;
  uint64_t maker_id = 0;
  uint32_t symbol = 0;
  int32_t price = 0;
  uint32_t qty = 0;
  uint8_t taker_side = kBuy;
};

struct alignas(64) Stats {
  uint64_t commands = 0;
  uint64_t trades = 0;
  uint64_t filled_qty = 0;
  uint64_t cancels = 0;
  uint64_t misses = 0;
  uint64_t expired = 0;
};

inline Stats& operator+=(Stats& a, const Stats& b) {
  a.commands += b.commands;
  a.trades += b.trades;
  a.filled_qty += b.filled_qty;
  a.cancels += b.cancels;
  a.misses += b.misses;
  a.expired += b.expired;
  return a;
}

struct Config {
  uint32_t symbols = 4096;
  uint64_t orders = 16000000;
  uint32_t threads = 0;
  uint32_t seed = 1;
  int32_t span = 16;
};

struct RunResult {
  Stats stats;
  double generate_seconds = 0;
  double match_seconds = 0;
  uint32_t threads = 0;
  uint64_t commands = 0;
};

uint32_t auto_threads();

std::vector<Command> generate_shard(uint32_t shard, uint32_t threads, const Config& cfg);
RunResult run_benchmark(const Config& cfg);
Stats run_commands(uint32_t symbols, const std::vector<Command>& commands, std::vector<Trade>* trades);
void write_trades_csv(const std::string& path, const std::vector<Trade>& trades);

}  // namespace apex
