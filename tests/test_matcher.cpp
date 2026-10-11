#include "apex/matcher.hpp"
#include "apex/types.hpp"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool cond, const char* expr, int line) {
  if (!cond) {
    std::fprintf(stderr, "fail %s:%d %s\n", __FILE__, line, expr);
    ++failures;
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

apex::Command limit_order(uint64_t id, uint32_t symbol, int32_t price, uint32_t qty, uint8_t side) {
  apex::Command cmd;
  cmd.id = id;
  cmd.symbol = symbol;
  cmd.price = price;
  cmd.qty = qty;
  cmd.side = side;
  cmd.op = apex::Op::NewLimit;
  return cmd;
}

void test_price_time() {
  apex::Matcher book(1, 0, 1);
  apex::Stats stats;
  std::vector<apex::Trade> trades;
  book.on_command(limit_order(1, 0, 100, 10, apex::kBuy), stats, &trades);
  book.on_command(limit_order(2, 0, 100, 10, apex::kBuy), stats, &trades);
  book.on_command(limit_order(3, 0, 100, 10, apex::kSell), stats, &trades);
  CHECK(trades.size() == 1);
  CHECK(trades[0].maker_id == 1);
  CHECK(trades[0].qty == 10);
  CHECK(trades[0].price == 100);
  CHECK(book.bid_volume(0) == 10);
  CHECK(book.ask_volume(0) == 0);
}

void test_partial_and_priority() {
  apex::Matcher book(1, 0, 1);
  apex::Stats stats;
  std::vector<apex::Trade> trades;
  book.on_command(limit_order(1, 0, 101, 5, apex::kBuy), stats, &trades);
  book.on_command(limit_order(2, 0, 100, 5, apex::kBuy), stats, &trades);
  book.on_command(limit_order(3, 0, 100, 3, apex::kSell), stats, &trades);
  CHECK(trades.size() == 1);
  CHECK(trades[0].maker_id == 1);
  CHECK(trades[0].price == 101);
  CHECK(trades[0].qty == 3);
  CHECK(book.best_bid(0) == 101);
  CHECK(book.bid_volume(0) == 7);
}

void test_sweep() {
  apex::Matcher book(1, 0, 1);
  apex::Stats stats;
  std::vector<apex::Trade> trades;
  book.on_command(limit_order(1, 0, 100, 3, apex::kSell), stats, &trades);
  book.on_command(limit_order(2, 0, 101, 3, apex::kSell), stats, &trades);
  book.on_command(limit_order(3, 0, 101, 5, apex::kBuy), stats, &trades);
  CHECK(trades.size() == 2);
  CHECK(trades[0].price == 100 && trades[0].qty == 3);
  CHECK(trades[1].price == 101 && trades[1].qty == 2);
  CHECK(book.ask_volume(0) == 1);
  CHECK(!book.has_bid(0));
}

void test_cancel_middle() {
  apex::Matcher book(1, 0, 1);
  apex::Stats stats;
  std::vector<apex::Trade> trades;
  book.on_command(limit_order(1, 0, 50, 1, apex::kBuy), stats, &trades);
  book.on_command(limit_order(2, 0, 50, 1, apex::kBuy), stats, &trades);
  book.on_command(limit_order(3, 0, 50, 1, apex::kBuy), stats, &trades);
  apex::Command cancel;
  cancel.op = apex::Op::Cancel;
  cancel.id = 2;
  cancel.symbol = 0;
  book.on_command(cancel, stats, &trades);
  book.on_command(limit_order(4, 0, 50, 2, apex::kSell), stats, &trades);
  CHECK(stats.cancels == 1);
  CHECK(trades.size() == 2);
  CHECK(trades[0].maker_id == 1);
  CHECK(trades[1].maker_id == 3);
  CHECK(book.bid_volume(0) == 0);
}

void test_no_cross_and_market() {
  apex::Matcher book(1, 0, 1);
  apex::Stats stats;
  std::vector<apex::Trade> trades;
  book.on_command(limit_order(1, 0, 99, 4, apex::kBuy), stats, &trades);
  book.on_command(limit_order(2, 0, 100, 4, apex::kSell), stats, &trades);
  CHECK(trades.empty());
  CHECK(book.best_bid(0) == 99);
  CHECK(book.best_ask(0) == 100);
  apex::Command market;
  market.op = apex::Op::NewMarket;
  market.id = 3;
  market.symbol = 0;
  market.qty = 4;
  market.side = apex::kBuy;
  book.on_command(market, stats, &trades);
  CHECK(trades.size() == 1);
  CHECK(trades[0].price == 100);
  CHECK(!book.has_ask(0));
  CHECK(book.has_bid(0));
}

void test_resting_cap() {
  apex::Matcher book(1, 0, 1);
  apex::Stats stats;
  for (uint32_t i = 0; i < apex::kMaxRestingPerSymbol + 20; ++i) {
    book.on_command(limit_order(i + 1, 0, 100 - static_cast<int32_t>(i), 1, apex::kBuy), stats, nullptr);
  }
  CHECK(book.resting(0) == apex::kMaxRestingPerSymbol);
  CHECK(book.bid_volume(0) == apex::kMaxRestingPerSymbol);
}

void test_generated_stream_does_not_cross() {
  apex::Config cfg;
  cfg.symbols = 32;
  cfg.orders = 20000;
  cfg.threads = 1;
  cfg.seed = 7;
  const std::vector<apex::Command> commands = apex::generate_shard(0, 1, cfg);
  apex::Matcher book(cfg.symbols, 0, 1);
  apex::Stats stats;
  for (const apex::Command& command : commands) {
    book.on_command(command, stats, nullptr);
  }
  CHECK(stats.trades > 0);
  for (uint32_t symbol = 0; symbol < cfg.symbols; ++symbol) {
    if (book.has_bid(symbol) && book.has_ask(symbol)) {
      CHECK(book.best_bid(symbol) < book.best_ask(symbol));
    }
    CHECK(book.resting(symbol) <= apex::kMaxRestingPerSymbol);
  }
}

void test_shards_cover_symbols() {
  apex::Config cfg;
  cfg.symbols = 8;
  cfg.orders = 4000;
  cfg.threads = 4;
  cfg.seed = 3;
  apex::Stats total;
  for (uint32_t shard = 0; shard < cfg.threads; ++shard) {
    const std::vector<apex::Command> commands = apex::generate_shard(shard, cfg.threads, cfg);
    apex::Matcher book(cfg.symbols, shard, cfg.threads);
    apex::Stats stats;
    for (const apex::Command& command : commands) {
      CHECK(command.symbol % cfg.threads == shard);
      book.on_command(command, stats, nullptr);
    }
    total += stats;
  }
  CHECK(total.trades > 0);
}

}  // namespace

int main() {
  test_price_time();
  test_partial_and_priority();
  test_sweep();
  test_cancel_middle();
  test_no_cross_and_market();
  test_resting_cap();
  test_generated_stream_does_not_cross();
  test_shards_cover_symbols();
  if (failures != 0) {
    std::fprintf(stderr, "%d checks failed\n", failures);
    return 1;
  }
  std::puts("apex tests passed");
  return 0;
}
