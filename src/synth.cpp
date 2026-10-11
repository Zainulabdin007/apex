#include "apex/types.hpp"

#include "apex/matcher.hpp"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <utility>

namespace apex {
namespace {

struct SymbolBook {
  int32_t mid = 100000;
  uint64_t ids[32]{};
  uint8_t count = 0;
};

uint32_t next_random(uint64_t& state) {
  state ^= state << 7;
  state ^= state >> 9;
  state ^= state << 8;
  return static_cast<uint32_t>(state);
}

}  // namespace

uint32_t auto_threads() {
  const unsigned cores = std::thread::hardware_concurrency();
  if (cores <= 1) {
    return 1;
  }
  return static_cast<uint32_t>(cores - 1);
}

std::vector<Command> generate_shard(uint32_t shard, uint32_t threads, const Config& cfg) {
  std::vector<Command> out;
  if (threads == 0 || shard >= threads || shard >= cfg.symbols || cfg.orders == 0) {
    return out;
  }
  const uint64_t begin = static_cast<uint64_t>(shard) * cfg.orders / threads;
  const uint64_t end = static_cast<uint64_t>(shard + 1) * cfg.orders / threads;
  const uint32_t books = (cfg.symbols - shard + threads - 1) / threads;
  out.reserve(static_cast<size_t>((end - begin) * 2 + 8));

  std::vector<SymbolBook> state(books);
  uint64_t rng = (static_cast<uint64_t>(cfg.seed) + 1ull) * 0x9e3779b97f4a7c15ull +
                 static_cast<uint64_t>(shard) * 0xbf58476d1ce4e5b9ull;
  uint64_t seq = 1;
  constexpr uint8_t kRing = 32;

  for (uint64_t n = begin; n < end; ++n) {
    const uint32_t local = books == 1 ? 0 : next_random(rng) % books;
    const uint32_t symbol = shard + local * threads;
    SymbolBook& book = state[local];
    const int step = static_cast<int>(next_random(rng) % 3) - 1;
    book.mid += step;
    if (book.mid < 1000) {
      book.mid = 1000;
    } else if (book.mid > 500000) {
      book.mid = 500000;
    }

    const uint32_t roll = next_random(rng) % 100;
    if (roll >= 70 && roll < 90 && book.count > 0) {
      const uint32_t slot = next_random(rng) % book.count;
      Command cancel{};
      cancel.op = Op::Cancel;
      cancel.id = book.ids[slot];
      cancel.symbol = symbol;
      out.push_back(cancel);
      book.ids[slot] = book.ids[book.count - 1];
      --book.count;
      continue;
    }

    Command cmd{};
    cmd.id = (static_cast<uint64_t>(shard) << 48) | seq++;
    cmd.symbol = symbol;
    cmd.qty = 1u + (next_random(rng) % 100u);
    cmd.side = (next_random(rng) & 1u) != 0 ? kSell : kBuy;
    if (roll >= 90) {
      cmd.op = Op::NewMarket;
      cmd.price = 0;
      out.push_back(cmd);
      continue;
    }

    cmd.op = Op::NewLimit;
    const uint32_t span = static_cast<uint32_t>(cfg.span < 0 ? 0 : cfg.span);
    const uint32_t offset = span == 0 ? 0 : next_random(rng) % (span + 1);
    const bool cross = (next_random(rng) & 1u) == 0;
    if (cmd.side == kBuy) {
      cmd.price = cross ? book.mid + static_cast<int32_t>(next_random(rng) % 3)
                        : book.mid - static_cast<int32_t>(offset);
    } else {
      cmd.price = cross ? book.mid - static_cast<int32_t>(next_random(rng) % 3)
                        : book.mid + static_cast<int32_t>(offset);
    }
    if (cmd.price < 1) {
      cmd.price = 1;
    }
    if (book.count == kRing) {
      Command cancel{};
      cancel.op = Op::Cancel;
      cancel.id = book.ids[0];
      cancel.symbol = symbol;
      out.push_back(cancel);
      for (uint8_t k = 1; k < kRing; ++k) {
        book.ids[k - 1] = book.ids[k];
      }
      book.count = static_cast<uint8_t>(kRing - 1);
    }
    out.push_back(cmd);
    book.ids[book.count++] = cmd.id;
  }
  return out;
}

Stats run_commands(uint32_t symbols, const std::vector<Command>& commands, std::vector<Trade>* trades) {
  Matcher matcher(symbols, 0, 1);
  matcher.reserve(commands.size());
  Stats stats;
  for (const Command& command : commands) {
    matcher.on_command(command, stats, trades);
  }
  return stats;
}

RunResult run_benchmark(const Config& cfg) {
  Config config = cfg;
  if (config.threads == 0) {
    config.threads = auto_threads();
  }
  if (config.symbols == 0) {
    config.symbols = 1;
  }
  if (config.threads > config.symbols) {
    config.threads = config.symbols;
  }

  std::vector<std::vector<Command>> batches(config.threads);
  std::vector<Stats> stats(config.threads);
  const auto generate_start = std::chrono::steady_clock::now();
  {
    std::vector<std::thread> threads;
    threads.reserve(config.threads);
    for (uint32_t shard = 0; shard < config.threads; ++shard) {
      threads.emplace_back([&, shard] { batches[shard] = generate_shard(shard, config.threads, config); });
    }
    for (std::thread& thread : threads) {
      thread.join();
    }
  }
  const auto match_start = std::chrono::steady_clock::now();
  {
    std::vector<std::thread> threads;
    threads.reserve(config.threads);
    for (uint32_t shard = 0; shard < config.threads; ++shard) {
      threads.emplace_back([&, shard] {
        Matcher matcher(config.symbols, shard, config.threads);
        matcher.reserve(batches[shard].size());
        Stats local;
        for (const Command& command : batches[shard]) {
          matcher.on_command(command, local, nullptr);
        }
        stats[shard] = local;
      });
    }
    for (std::thread& thread : threads) {
      thread.join();
    }
  }
  const auto match_end = std::chrono::steady_clock::now();

  RunResult result;
  result.threads = config.threads;
  for (uint32_t shard = 0; shard < config.threads; ++shard) {
    result.commands += batches[shard].size();
    result.stats += stats[shard];
  }
  result.generate_seconds = std::chrono::duration<double>(match_start - generate_start).count();
  result.match_seconds = std::chrono::duration<double>(match_end - match_start).count();
  return result;
}

void write_trades_csv(const std::string& path, const std::vector<Trade>& trades) {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    throw std::runtime_error("could not open " + path);
  }
  std::fputs("seq,ts,symbol,side,price,size,is_implied,payload\n", file);
  uint64_t seq = 1;
  uint64_t ts = 1700000000000000ull;
  for (const Trade& trade : trades) {
    char symbol[32];
    std::snprintf(symbol, sizeof(symbol), "S%06u", trade.symbol);
    const double price = static_cast<double>(trade.price) / 100.0;
    std::fprintf(file, "%llu,%llu,%s,%s,%.8f,%u,false,\n", static_cast<unsigned long long>(seq),
                 static_cast<unsigned long long>(ts), symbol, trade.taker_side == kBuy ? "buy" : "sell", price,
                 trade.qty);
    ++seq;
    ts += 1000;
  }
  std::fclose(file);
}

}  // namespace apex
