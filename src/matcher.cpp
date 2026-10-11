#include "apex/matcher.hpp"

#include <cassert>
#include <utility>

namespace apex {
namespace {

uint32_t local_count(uint32_t symbols, uint32_t shard, uint32_t threads) {
  if (threads == 0 || shard >= symbols) {
    return 0;
  }
  return (symbols - shard + threads - 1) / threads;
}

}  // namespace

Matcher::Side::Side(bool bids) : bids_(bids) {}

bool Matcher::Side::empty() const { return prices_.empty(); }

int32_t Matcher::Side::best() const {
  assert(!prices_.empty());
  return prices_.front();
}

Matcher::Level& Matcher::Side::front() {
  assert(!levels_.empty());
  return levels_.front();
}

uint64_t Matcher::Side::volume() const {
  uint64_t total = 0;
  for (const Level& level : levels_) {
    total += level.volume;
  }
  return total;
}

int Matcher::Side::find(int32_t price) const {
  int lo = 0;
  int hi = static_cast<int>(prices_.size());
  while (lo < hi) {
    const int mid = (lo + hi) >> 1;
    const int32_t current = prices_[static_cast<size_t>(mid)];
    if (current == price) {
      return mid;
    }
    if (bids_ ? current > price : current < price) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return -1;
}

int Matcher::Side::insert_pos(int32_t price) const {
  int lo = 0;
  int hi = static_cast<int>(prices_.size());
  while (lo < hi) {
    const int mid = (lo + hi) >> 1;
    const bool worse = bids_ ? prices_[static_cast<size_t>(mid)] < price
                             : prices_[static_cast<size_t>(mid)] > price;
    if (worse) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return lo;
}

Matcher::Level& Matcher::Side::level_at(int index) { return levels_[static_cast<size_t>(index)]; }

Matcher::Level& Matcher::Side::level_for(int32_t price) {
  const int existing = find(price);
  if (existing >= 0) {
    return levels_[static_cast<size_t>(existing)];
  }
  const int pos = insert_pos(price);
  prices_.insert(prices_.begin() + pos, price);
  levels_.insert(levels_.begin() + pos, Level{});
  return levels_[static_cast<size_t>(pos)];
}

void Matcher::Side::erase_at(int index) {
  prices_.erase(prices_.begin() + index);
  levels_.erase(levels_.begin() + index);
}

Matcher::Matcher(uint32_t symbols, uint32_t shard, uint32_t threads)
    : symbols_(symbols), shard_(shard), threads_(threads == 0 ? 1 : threads) {
  const uint32_t books = local_count(symbols_, shard_, threads_);
  bids_.reserve(books);
  asks_.reserve(books);
  for (uint32_t i = 0; i < books; ++i) {
    bids_.emplace_back(true);
    asks_.emplace_back(false);
  }
  resting_.assign(books, 0);
  slots_.assign(1024, Slot{});
  mask_ = 1023;
}

void Matcher::reserve(uint64_t orders) {
  if (orders > orders_.capacity()) {
    orders_.reserve(static_cast<size_t>(orders));
  }
  uint32_t cap = 1024;
  const uint64_t limit = orders + 1;
  while (static_cast<uint64_t>(cap) * 7 < limit * 10) {
    if (cap >= 0x40000000u) {
      break;
    }
    cap *= 2;
  }
  if (slots_.size() < cap && live_ == 0) {
    slots_.assign(cap, Slot{});
    mask_ = cap - 1;
    used_ = 0;
  }
}

bool Matcher::owns(uint32_t symbol) const {
  return symbol < symbols_ && symbol % threads_ == shard_;
}

uint32_t Matcher::book_index(uint32_t symbol) const { return (symbol - shard_) / threads_; }

uint32_t Matcher::mix(uint64_t id) {
  uint64_t x = id + 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  x = x ^ (x >> 31);
  return static_cast<uint32_t>(x);
}

void Matcher::id_grow() {
  uint32_t cap = slots_.empty() ? 1024u : static_cast<uint32_t>(slots_.size() * 2);
  std::vector<Slot> next(cap);
  const uint32_t next_mask = cap - 1;
  for (const Slot& slot : slots_) {
    if (slot.state != 1) {
      continue;
    }
    uint32_t i = mix(slot.id) & next_mask;
    while (next[i].state == 1) {
      i = (i + 1) & next_mask;
    }
    next[i] = slot;
  }
  slots_.swap(next);
  mask_ = next_mask;
  used_ = live_;
}

void Matcher::id_put(uint64_t id, uint32_t order) {
  if (slots_.empty() || (used_ + 1) * 10 >= slots_.size() * 7) {
    id_grow();
  }
  uint32_t i = mix(id) & mask_;
  uint32_t tomb = kNil;
  for (uint32_t n = 0; n < slots_.size(); ++n) {
    Slot& slot = slots_[i];
    if (slot.state == 0) {
      const uint32_t at = tomb == kNil ? i : tomb;
      if (tomb == kNil) {
        ++used_;
      }
      slots_[at] = Slot{id, order, 1};
      ++live_;
      return;
    }
    if (slot.state == 2 && tomb == kNil) {
      tomb = i;
    } else if (slot.state == 1 && slot.id == id) {
      slot.order = order;
      return;
    }
    i = (i + 1) & mask_;
  }
  id_grow();
  id_put(id, order);
}

uint32_t Matcher::id_find(uint64_t id) const {
  if (slots_.empty()) {
    return kNil;
  }
  uint32_t i = mix(id) & mask_;
  for (uint32_t n = 0; n < slots_.size(); ++n) {
    const Slot& slot = slots_[i];
    if (slot.state == 0) {
      return kNil;
    }
    if (slot.state == 1 && slot.id == id) {
      return slot.order;
    }
    i = (i + 1) & mask_;
  }
  return kNil;
}

void Matcher::id_erase(uint64_t id) {
  if (slots_.empty()) {
    return;
  }
  uint32_t i = mix(id) & mask_;
  for (uint32_t n = 0; n < slots_.size(); ++n) {
    Slot& slot = slots_[i];
    if (slot.state == 0) {
      return;
    }
    if (slot.state == 1 && slot.id == id) {
      slot.state = 2;
      slot.order = kNil;
      if (live_ > 0) {
        --live_;
      }
      return;
    }
    i = (i + 1) & mask_;
  }
}

uint32_t Matcher::pool_alloc(const Order& proto) {
  uint32_t index;
  if (!free_.empty()) {
    index = free_.back();
    free_.pop_back();
    orders_[index] = proto;
  } else {
    index = static_cast<uint32_t>(orders_.size());
    orders_.push_back(proto);
  }
  orders_[index].live = 1;
  orders_[index].prev = kNil;
  orders_[index].next = kNil;
  return index;
}

void Matcher::pool_free(uint32_t index) {
  orders_[index].live = 0;
  free_.push_back(index);
}

Matcher::Order& Matcher::order_at(uint32_t index) { return orders_[index]; }

void Matcher::consume_level(Side& opposite, uint32_t book, uint32_t& qty, int32_t price, const Command& taker,
                            Stats& stats, std::vector<Trade>* trades) {
  while (qty > 0 && !opposite.empty() && opposite.best() == price) {
    Level& level = opposite.front();
    const uint32_t head = level.head;
    Order& maker = order_at(head);
    const uint32_t take = qty < maker.qty ? qty : maker.qty;
    const uint64_t maker_id = maker.id;
    if (trades != nullptr) {
      trades->push_back(Trade{taker.id, maker_id, taker.symbol, price, take, taker.side});
    }
    ++stats.trades;
    stats.filled_qty += take;
    qty -= take;
    maker.qty -= take;
    level.volume -= take;
    if (maker.qty == 0) {
      const uint32_t next = maker.next;
      level.head = next;
      if (next == kNil) {
        level.tail = kNil;
      } else {
        order_at(next).prev = kNil;
      }
      maker.live = 0;
      id_erase(maker_id);
      pool_free(head);
      if (resting_[book] > 0) {
        --resting_[book];
      }
      if (level.head == kNil) {
        opposite.erase_at(0);
      }
    }
  }
}

void Matcher::rest(uint32_t book, const Command& cmd, Stats& stats) {
  if (resting_[book] >= kMaxRestingPerSymbol) {
    ++stats.expired;
    return;
  }
  Order proto;
  proto.id = cmd.id;
  proto.price = cmd.price;
  proto.qty = cmd.qty;
  proto.side = cmd.side;
  const uint32_t index = pool_alloc(proto);
  id_put(cmd.id, index);
  Side& side = cmd.side == kBuy ? bids_[book] : asks_[book];
  Level& level = side.level_for(cmd.price);
  Order& order = order_at(index);
  order.prev = level.tail;
  order.next = kNil;
  if (level.tail != kNil) {
    order_at(level.tail).next = index;
  } else {
    level.head = index;
  }
  level.tail = index;
  level.volume += cmd.qty;
  ++resting_[book];
}

void Matcher::take(Command cmd, bool limit, Stats& stats, std::vector<Trade>* trades) {
  const uint32_t book = book_index(cmd.symbol);
  Side& opposite = cmd.side == kBuy ? asks_[book] : bids_[book];
  while (cmd.qty > 0 && !opposite.empty()) {
    const int32_t best = opposite.best();
    if (limit) {
      if (cmd.side == kBuy && best > cmd.price) {
        break;
      }
      if (cmd.side == kSell && best < cmd.price) {
        break;
      }
    }
    consume_level(opposite, book, cmd.qty, best, cmd, stats, trades);
  }
  if (limit && cmd.qty > 0) {
    rest(book, cmd, stats);
  }
}

void Matcher::cancel(const Command& cmd, Stats& stats) {
  const uint32_t index = id_find(cmd.id);
  if (index == kNil) {
    ++stats.misses;
    return;
  }
  Order& order = order_at(index);
  const uint32_t book = book_index(cmd.symbol);
  Side& side = order.side == kBuy ? bids_[book] : asks_[book];
  const int level_index = side.find(order.price);
  if (level_index < 0) {
    ++stats.misses;
    return;
  }
  Level& level = side.level_at(level_index);
  const uint32_t prev = order.prev;
  const uint32_t next = order.next;
  if (prev != kNil) {
    order_at(prev).next = next;
  } else {
    level.head = next;
  }
  if (next != kNil) {
    order_at(next).prev = prev;
  } else {
    level.tail = prev;
  }
  if (level.volume >= order.qty) {
    level.volume -= order.qty;
  } else {
    level.volume = 0;
  }
  const uint64_t id = order.id;
  pool_free(index);
  id_erase(id);
  if (resting_[book] > 0) {
    --resting_[book];
  }
  ++stats.cancels;
  if (level.head == kNil) {
    side.erase_at(level_index);
  }
}

void Matcher::on_command(const Command& cmd, Stats& stats, std::vector<Trade>* trades) {
  if (!owns(cmd.symbol) || (cmd.op != Op::Cancel && cmd.qty == 0)) {
    ++stats.misses;
    return;
  }
  ++stats.commands;
  if (cmd.op == Op::Cancel) {
    cancel(cmd, stats);
  } else if (cmd.op == Op::NewMarket) {
    take(cmd, false, stats, trades);
  } else {
    take(cmd, true, stats, trades);
  }
}

bool Matcher::has_bid(uint32_t symbol) const {
  if (!owns(symbol)) {
    return false;
  }
  return !bids_[book_index(symbol)].empty();
}

bool Matcher::has_ask(uint32_t symbol) const {
  if (!owns(symbol)) {
    return false;
  }
  return !asks_[book_index(symbol)].empty();
}

int32_t Matcher::best_bid(uint32_t symbol) const { return bids_[book_index(symbol)].best(); }

int32_t Matcher::best_ask(uint32_t symbol) const { return asks_[book_index(symbol)].best(); }

uint64_t Matcher::bid_volume(uint32_t symbol) const { return bids_[book_index(symbol)].volume(); }

uint64_t Matcher::ask_volume(uint32_t symbol) const { return asks_[book_index(symbol)].volume(); }

uint32_t Matcher::resting(uint32_t symbol) const { return resting_[book_index(symbol)]; }

}  // namespace apex
